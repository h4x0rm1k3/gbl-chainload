# Swallow Capability + Oplus fastboot-sec Analysis — Track 3 Design

**Status:** approved — ready for implementation plan.

**Depends on:** Track 1 (`2026-05-10-logfs-verbosity-audit-design.md`)
merged first so swallow/mutate/observe decisions land in
`GblChainload_BootN.txt` reliably during validation.

## Goal

1. Identify the specific calls and partition writes that oplus
   fastboot-sec relies on for region/lock-state gating (common base
   between OOS and ColorOS).
2. Build a reusable swallow/mutate/observe capability in our hooks so
   we can intercept those calls. Capability is generic — other OEM
   gates can be addressed with the same primitive.
3. Produce a CN-tester data-capture procedure so the strategy can be
   validated on a device we don't own.

## Approach (4 sub-tasks)

### Sub-task 1 — RE pass: locate the gates

Sources to grep:

- `~/gbl-chainload-dirty/logs/*/` (117 captures, especially
  `bootloader_log` + `logfs/UefiLog*.txt` + `dmesg.txt`). UefiLog
  captures the entire pre-GBL chain — region/lock decisions are often
  emitted there.
- `.re-notes/sessions/2026-05-07-qseecom-callgraph.md`
- `.re-notes/sessions/2026-05-08-repo-audit-fastboot-t2-readiness.md`
- `docs/re/fakelock-vs-debug-comparison.md`
- ABL disassembly via Ghidra MCP
- **Live oplusreserve1 contents**: while the test device is mounted,
  pull `/dev/block/by-name/oplusreserve1` via `dd | adb pull` and
  parse its structure (hex dump, look for known offsets, compare
  pre/post unlock).

What to look for:

- `oplusreserve1` reads/writes (block device path, fread/fwrite from
  ABL, byte offsets that change on lock/unlock)
- Region/locale gating decisions (`region`, `regionalhybrid`,
  country-code branches in ABL)
- Lock-state propagation to stock fastboot (`SetDeviceUnlockValue`,
  `GetDeviceUnlockValue`, RPMB writes)
- TA names involved (`fastbootlog`, `keymaster`, anti-rollback)
- SCM SIPs around lock/unlock transitions

Output: `docs/re/oplus-fastboot-sec-mechanism.md` — a findings doc
listing each gate, the call(s) or write(s) that implement it, and the
recommended intercept point per gate. Candidate intercept points:

- **SCM / qseecom hook** for SMC-issued gates
- **BlockIo protocol hook** for `oplusreserve1` partition writes
  (install our own `EFI_BLOCK_IO_PROTOCOL` wrapper on the partition
  handle and gate Write calls) — cleaner than SCM gymnastics when the
  gate is just "write to this partition"
- **ABL binary patch** for pre-DXE gates we can't reach from DXE

### Sub-task 2 — build the swallow capability

Refactor `ProtocolHookLib` from log+passthrough to a three-action API:

- **observe** (current behaviour) — log, passthrough
- **swallow** — log, skip the original, return synthetic success
- **transform** — log, call original, mutate the response, return

API shape (sketch):

```c
typedef enum {
  HookActionObserve,
  HookActionSwallow,
  HookActionTransform,
} HOOK_ACTION;

typedef struct {
  UINT32       Match;     // SIP id, TA cmd id, BlockIo LBA range, etc.
  HOOK_ACTION  Action;
  EFI_STATUS   (*Transform)(IN OUT VOID *Args, IN OUT VOID *Result);
  CONST CHAR8 *Why;        // for logging
} HOOK_RULE;
```

Each hook (ScmHook, QseecomHook, SpssHook, VerifiedBootHook, and a new
BlockIoHook) gets its own rule table. Rule tables live in dedicated
files under `GblChainloadPkg/Library/ProtocolHookLib/Rules/` so they're
easy to audit and per-OEM specialisation is mechanical.

Logs from Track 1's widened verbosity will show every rule decision
(observe / swallow / transform + the rule's `Why`) for each hooked
call.

The refactor is the bulk of this sub-task. Cross-OEM reuse is an
explicit goal — the API should be agnostic to the specific gates of
any one OEM.

### Sub-task 3 — concrete rules for fastboot-sec

Based on sub-task 1's findings, populate the rule tables to block or
transform exactly the calls / writes that implement oplus
fastboot-sec. Mode-gated:

- mode-1 / mode-fakelocked: rules active
- mode-0: rules dormant (so we can A/B compare)

Examples likely to land (concrete list comes from sub-task 1):

- Swallow `oplusreserve1` writes at byte offsets that propagate
  locked/unlocked telemetry (BlockIo hook)
- Synthesize "still locked" responses for any GetDeviceUnlockValue
  call after fastboot session
- Block the specific TA cmd that programs region/lock state to secure
  storage

### Sub-task 4 — CN-tester validation procedure

Since we don't own a CN device, we ship a documented procedure for a
CN tester:

1. **Pre-test capture** (stock CN device, before any patches):
   - `fastboot getvar all`
   - `fastboot oem device-info` (or equivalent)
   - One full `test-device.sh` run with our mode-0 EFI (observe-only)
2. **Test capture** (our mode-1 EFI with swallow rules active):
   - Same captures
3. **Diff procedure** — what fields should change vs stay the same,
   with concrete pass/fail criteria

Doc: `docs/re/oplus-fastboot-sec-cn-validation.md` — written so a CN
tester can follow without project context. Includes the EFI binary
hash + commit ref so the tester captures match a known build.

## Validation on global device (CPH2747EEA)

What we can confirm:

- Swallow capability dispatches correctly (rule fires for the matched
  call, log line lands in GblChainload_BootN.txt with the right `Why`)
- Mode gating works (mode-0 leaves rules dormant, mode-1 activates)
- We don't break global-device boot

What we cannot confirm:

- That fastboot-sec is actually blocked — CN-only behaviour.

## Out of scope

- Implementing transform helpers for every conceivable TA cmd (only
  the ones sub-task 1 identifies)
- Lock-state changes that involve user data wipe (separate concern)
- Modifying ABL itself for runtime gates (we operate at DXE; ABL
  binary patches are listed as a candidate intercept point but the
  binary-patch tooling work is a separate plan if it's needed)

## Files in scope

- `docs/re/oplus-fastboot-sec-mechanism.md` (new — sub-task 1 output)
- `docs/re/oplus-fastboot-sec-cn-validation.md` (new — sub-task 4 output)
- `GblChainloadPkg/Library/ProtocolHookLib/ScmHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/QseecomHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/SpssHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/BlockIoHook.c` (new)
- `GblChainloadPkg/Library/ProtocolHookLib/Rules/*.c` (new directory)
- `GblChainloadPkg/Library/ProtocolHookLib/HookCommon.h` (extend with
  HOOK_RULE / HOOK_ACTION definitions)
- `GblChainloadPkg/Library/ProtocolHookLib/InstallAll.c` (register
  BlockIoHook)
- `GblChainloadPkg/GblChainloadPkg.dec` (if any new library classes)

## Risks and known gotchas

- Sub-task 1 might conclude that gating is entirely in ABL pre-DXE,
  in which case our DXE-level swallow can't help and we need an ABL
  binary patch plan instead. The findings doc should explicitly state
  which intercept layer each gate needs.
- Sub-task 1 might find nothing concrete in existing dirty-repo
  captures. If so, capture freshly with Track 1's verbosity active
  before sub-task 2 starts.
- BlockIo hook is well-trodden territory in EDK2 but our existing
  hooks don't yet wrap a vtable that touches block storage —
  introducing a new hook is a non-trivial addition.
- The rule-table abstraction is easy to over-engineer. Hold the line
  at three actions (observe/swallow/transform); don't add policy
  combinators, hot-reload, etc., until a second OEM actually needs
  them.
