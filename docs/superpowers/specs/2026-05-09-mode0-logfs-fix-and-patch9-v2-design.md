# Mode-0 + logfs always-works + patch9 v2 — design

**Date**: 2026-05-09
**Status**: Design draft; awaiting user review.
**Working directory**: `/home/vivy/gbl-chainload` (v2 repo, `main` at `bd92712` after plan-1 + post-plan-1 fixes).

## Context

Plan-1 shipped (`v2.0.0-plan1-foundation`). Device test of `dist/mode-1.efi` on the user's sm8850-platform device (codename TBD between mihon/infiniti/other) revealed three real issues:

1. **logfs failed to mount.** The post-plan-1 ordering fix (LogFsInit moved after `LoadDriversFromCurrentFv`) didn't resolve it. v1's mode-fakelocked successfully mounted logfs on what is presumably the same device, so this is a v2 regression — but the carry-forward audit found LogFsLib byte-identical and `.dsc` library mappings complete. The diff lies elsewhere (likely in the conflict-resolved cherry-pick of `2c4c2d629c FastbootLib: add recovery escape controls`, where the efi-load/start/unload/status block was excluded as it depended on the dropped `676e4887a7 staged EFI helpers` — but that block may have included setup that LogFsLib relies on).
2. **patch9 missed on device.** Per-patch logging (now shipped) confirmed: patch1 OK, patch7 OK, patch9 MISS. Our 32-byte anchors against `LinuxLoader_infiniti.efi` (gbl_root_canoe's older fixture) didn't survive on the user's actual on-device ABL — likely a different sm8850-platform build (e.g. EU 16.0.5.703).
3. **Fastboot menu had stale "Toggle Primary Boot OS" + missing v2 flag-state display.** Already fixed in commits `83de0a170a` (edk2) + `bd92712` (submodule bump). Out of scope for this spec.

Beyond fixing 1 and 2, this spec adds **mode-0** as a foundation baseline: a minimal build target that does AblUnwrap → patch1 → LoadImage with no protocol hooks and no patch9. Mode-0 acts as the chain-load pipeline's truth-test — if it boots cleanly, we know the build infrastructure / chain mechanism / patch1 work, isolating mode-1's failures to its specific overlay.

## Design goals

1. **Mode-0 minimal baseline** — proves chain-load works independent of mode-1 specifics. Used as control case for diagnostic isolation and as a recovery payload if mode-1 ever bricks.
2. **logfs always works** — across mode-0, mode-1, and future mode-2/3. Per user direction: not gated by mode. Includes a "proper transition" for chained-EFI handoff (clean unmount before LoadImage so the next EFI in the chain isn't blocked).
3. **patch9 v2 — fewer sites, multi-binary durable** — replaces the 3-site current patch9 with a 2-site (likely) design that's anchored to be unique across infiniti (old + EU 16.0.5.703) + mihon, while leveraging the libavb-audit insight that color/state is ABL-side-only and the protocol hook is the authoritative control.

## Architecture overview

```
   GBL .efi (mode-0/1/2/3)
        │
        ├─ CommonEarlyInit
        │     ├─ DeviceInfoInit + EnumeratePartitions + UpdatePartitionEntries
        │     ├─ LoadDriversFromCurrentFv
        │     └─ LogFsInit (ALWAYS attempted — works in every mode)
        │
        ├─ key-window (AUTO toggle)
        │
        ▼
   BootFlowChainLoad
        │
        ├─ AblUnwrap_LoadFromPartition
        │
        ├─ DynamicPatch_Apply
        │     ├─ patch1 [universal, mandatory] — every mode
        │     ├─ patch7 [oem-oneplus, mode-1+ optional] — ARCHIVED, not in active table
        │     └─ patch9 [mode-1, mandatory] — only when GBL_MODE>=1
        │
        ├─ ProtocolHook_InstallAll  — only when GBL_MODE>=1
        │     ├─ Universal baseline (VB swallow, SCM fuse drop, OplusSec 0x0A drop)
        │     └─ Mode-N overlay (mode-1: VB READ_CONFIG mutate + VBDeviceInit clear)
        │
        ├─ LogFsFlush + LogFsClose ("proper transition" — release before LoadImage)
        │
        └─ LoadImage + StartImage
```

Mode-0 path: `CommonEarlyInit → BootFlowChainLoad → AblUnwrap → patch1 → LogFs flush+close → LoadImage`. No protocol hooks, no patch9.

Mode-1 path: same plus protocol hook installation and patch9.

Modes 2 and 3 are deferred to plan-2 / plan-3 (covered in their own specs).

## Section 1 — Mode-0 baseline

### What it does

Mode-0 is the absolute minimum chain-load. It:

- Mounts logfs (always — see §2).
- Applies patch1 only (universal, mandatory).
- Skips ProtocolHook_InstallAll entirely.
- Skips patch9.
- Calls LoadImage on the patched ABL.

End state: device boots stock ABL, with `efisp` partition recursion broken (patch1) so chained EFI doesn't loop.

### What it doesn't do

- No fakelock mutation (ABL sees real device-info — unlocked).
- No SCM/OplusSec drops (writes proceed).
- No libavb continue-permissive (ABL is strict — but ABL also sees the device as unlocked, so libavb gets `AllowVerificationError=TRUE` naturally and is permissive on its own).

Important: mode-0 on a custom-recovery boot **will** produce orange/unlocked output to KM/Oplus/cmdline. Mode-0 isn't for production fakelock; it's for chain-load validation.

### Build flag

`GBL_MODE=0` is a new value. The PatchTable aggregator gates mode-1 patches behind `#if (GBL_MODE >= 1)` (was `==1`). Universal patches (patch1) are always included.

In `BootFlow.c`, ProtocolHook_InstallAll is gated behind `#if (GBL_MODE >= 1)`. For GBL_MODE==0, the hook install step is skipped entirely; no UniversalBaseline policy hooks (those are only useful when ABL is fakelocked).

In `Entry.c`, no changes — mode-0 uses the same AUTO/DEBUG/VERBOSE flag plumbing.

### Default artifacts

- `dist/mode-0.efi` — production silent (AUTO=0, DEBUG=0, VERBOSE=0).
- `dist/mode-0-auto.efi` — host-driven test (AUTO=1).
- `dist/mode-0-auto-debug-verbose.efi` — full dev capture.

### Verification path

- Stage `dist/mode-0-auto.efi` via `fastboot stage` + `fastboot oem boot-efi`. Wait for FastbootLib menu, then `fastboot oem escape`.
- Expected: chainload triggers; `DynamicPatch: patch1 [universal, mandatory] -> OK` in bootloader_log; LoadImage succeeds; device boots stock ABL → custom recovery (with real on-disk content; ABL/userspace see orange/unlocked because no fakelock).
- This validates: build pipeline, fastboot escape path, AblUnwrap, patch1, LoadImage, logfs mount.

## Section 2 — logfs always works

### Requirement

Per user direction: logfs mounts in every mode. No `#if GBL_MODE != 0` gate around LogFsInit.

### Investigation framework (implementation phase)

The post-plan-1 ordering fix (`LogFsInit` after `LoadDriversFromCurrentFv`) didn't resolve the `connectcontroller (Not Found)` failure. The audit ruled out:
- LogFsLib code drift (byte-identical to v1).
- Missing `.dsc` library mappings.
- Dropped commit `676e4887a7` touching logfs/partition.

What hasn't been ruled out:
- The Task 5 conflict-resolved exclusion of the efi-load/start/unload/status block from `2c4c2d629c FastbootLib: add recovery escape controls`. That block may have included a partition pre-bind / FAT driver connect step.
- Edge cases in our v2's CommonEarlyInit ordering relative to the chained-EFI handoff state from the user's permanent EFISP.
- A `[LibraryClasses]` mapping in `GblChainloadPkg.dsc` that's present in `[LibraryClasses]` but bound to a different module type than what LogFsLib expects at runtime.

### Plan

Implementation of logfs-fix follows this dependency order:

1. **Add diagnostic verbosity to `LogFsInit`.** Print every step it tries (find partition handle, locate BlockIO protocol, ConnectController call, result). Build a one-off `dist/mode-0-logfs-debug.efi` (mode-0 with maximum LogFs init verbosity to stdout/screen).
2. **User stages mode-0-logfs-debug.efi on device.** Captures the exact failure point. Pulls log via adb post-boot.
3. **Diagnose from device evidence.** Three probable diagnoses:
   - (a) ConnectController fails because no driver is bound for FAT — fix: explicitly load FAT driver from our payload's FV before LogFsInit.
   - (b) Partition handle has no BlockIO protocol — fix: add explicit partition-protocol probing/install.
   - (c) The previous EFI in the chain (user's old auto_debug_mode permanent EFISP) left the partition in a state where re-mount is rejected — fix: explicit DisconnectController + ConnectController sequence.
4. **Apply the diagnosed fix.** Per the slop guidance: no ad-hoc workaround shims; the fix must address the actual root cause identified in step 3.
5. **Add the "proper transition"**: in BootFlow.c, after patches apply and before LoadImage, call `LogFsFlush()` then `LogFsClose()` to release the partition handle. This ensures any chained EFI (the patched ABL or further-chained payloads) can re-mount.
6. **Re-test mode-0-auto.efi end-to-end.** Logfs must mount; logs must be writeable; partition must be releasable for the next EFI.

### Implementation phase note

Section 2's specific fix is determined by step 3's diagnosis. The spec commits to "logfs always works" as a requirement, with steps 1-2 as the diagnostic gate. If step 3 reveals a deep architectural issue (e.g., the v2 build genuinely can't include the FAT driver in its FV because of EDK-II module-type constraints), this spec needs revision — but the most likely outcome is one of the three (a/b/c) fix paths, each of which is a 1-3 line code change.

## Section 3 — Patch7 archived

`patch7-orange-screen` stays at `oem/oneplus_canoe.c` with full PATCH_DESC source intact. The `kOemOneplusPatches[]` entry is commented out (or guarded behind a conditional like `#ifdef GBL_PATCH7_ENABLED`) so it's not in any active mode's table by default.

Rationale: under mode-1's fakelock, ABL acts locked → orange screen never fires → patch7 is dead code at runtime. Keeping the source as a diagnostic asset (re-enable as a one-line revert if libavb patching goes severely wrong, to probe ABL's lock-state output behavior).

## Section 4 — Patch9 v2

### Design (informed by libavb audit)

The libavb source-tree audit revealed:

- `AvbSlotVerifyData` (libavb's return struct, `avb_slot_verify.h:299-308`) has **no color/state field**. libavb does not compute or write boot-state color. Only the cmdline buffer it returns includes a `vbmeta.device_state="locked"|"unlocked"` token, and that's read from the `read_is_device_unlocked` callback (which the protocol hook controls).
- ABL's color/state computation (`VerifiedBoot.c:1728-1736`) is **purely** `if (AllowVerificationError) BootState = ORANGE; else BootState = (UserKey ? YELLOW : GREEN)`.
- `AllowVerificationError` is initialized at `VerifiedBoot.c:1364` from `IsUnlocked()`. Mode-1's protocol hook fakelocks `IsUnlocked() → FALSE`, so `AllowVerificationError = FALSE` automatically, so color = GREEN automatically.

**Critical insight:** the protocol hook is already the single authoritative control for color. We do **not** need any patch9 site to preserve color. We do need patches to:

1. Make libavb permissive (so it returns populated SlotData on recoverable failures like `OK_NOT_SIGNED`).
2. Make ABL's post-libavb gate not fatal on a non-OK Result.

Color stays GREEN naturally because we never touch the `AllowVerificationError` bool at its color-computation site.

### Approach A — VerifyFlags + post-libavb gate

Two rewrite sites:

- **Site V (VerifyFlags derivation)** at `VerifiedBoot.c:1379-1381` source-equivalent. The compiled instructions derive `VerifyFlags = AllowVerificationError ? AVB_SLOT_VERIFY_FLAGS_ALLOW_VERIFICATION_ERROR : 0`. Patch the derivation so `VerifyFlags` always has the flag bit set, **regardless of `AllowVerificationError`**. Compiler typically emits one of:
  - `csel Wflags, Wone, Wzr, ne` — patch to `mov Wflags, #1`
  - `cmp Wbool, #0; mov Wflags, #0; b.eq +4; mov Wflags, #1` — patch to `mov Wflags, #1` at the call site
  - `and Wflags, Wbool, #1` — patch to `mov Wflags, #1`
  
  Anchor RE: locate the `bl avb_slot_verify` (or `bl avb_slot_verify_full`) call site. The instruction setting the flag arg (typically `w2`) is within the few instructions before. Pattern: a 12-16 byte sequence around the call site.

- **Site G (post-libavb gate)** at `VerifiedBoot.c:1591-1605` source-equivalent. The compiled instructions implement `if (AllowVerificationError && ResultShouldContinue(Result)) {continue} else if (Result != OK) {fatal} else {OK rollback path}`. Patch the gate so the `else if (Result != OK) {fatal}` branch is skipped — i.e., always continue when ResultShouldContinue is TRUE, regardless of `AllowVerificationError`.
  
  Possible patches:
  - Force the gate's bool test to always-true (rewrite the `cbz Wbool, fatal` → `nop`).
  - Rewrite the fatal-path branch target so non-fatal results don't reach it.
  
  The simpler (one-instruction) fix is preferred: turn the gate's bool-test cbz into a no-op. Gate then proceeds to the recoverable-continue path always.
  
  Anchor RE: pattern around the post-libavb result classifier — likely involves a `bl ResultShouldContinue` or `cmp Wresult, #` followed by branch instructions.

### Approach B (architecturally distinct) — libavb-internal return remap

Approach B is **not** a layered fallback on top of Approach A. It's a different patch architecture: instead of patching ABL's wrapper at two sites (Site V + Site G), it patches **libavb itself** at one site — the return-value setup in `avb_slot_verify_full` — so any recoverable result (e.g., `OK_NOT_SIGNED`) becomes `OK` at function exit. ABL then sees `Result=OK` always when SlotData is populated and takes the OK fast path naturally, with no need to alter ABL's gate.

Approach A and Approach B are mutually exclusive. Selection criteria:

- **Choose Approach A** if the disassembly of Site V and Site G produces clean, unique 16-32 byte anchors that match across all three target binaries with a single pattern each. Two ABL-side rewrites; libavb untouched.
- **Choose Approach B** if Site V or Site G's anchors are unstable across the binaries (e.g., compiler-emitted instruction reorderings) AND libavb's return-value setup has a stable anchor. One libavb-side rewrite; ABL untouched.

libavb is short and source-stable (Qualcomm rarely customizes it), which can make Approach B more cross-binary durable. The trade-off is RE complexity: `avb_slot_verify_full` has multiple return paths; the patch must target only the recoverable-result return paths, not the catastrophic-failure ones.

The implementation phase performs disassembly across all three binaries first, then commits to A or B based on which pattern is cleaner. The choice is data-driven, not iterative.

### Fixture inventory (multi-binary anchor target set)

| Fixture path | Device | Region | OS Version | Format | Has GBL loader? |
|---|---|---|---|---|---|
| `images/infiniti/LinuxLoader_infiniti.efi` | infiniti | global | (older, gbl_root_canoe era) | unwrapped PE | yes (assumed; gbl_root_canoe targeted it) |
| `images/infiniti-EU-16.0.5.703/abl.bin` | infiniti | EU | 16.0.5.703 | raw FV | likely yes |
| `images/infiniti-IN-16.0.7.201.img` | infiniti | IN | 16.0.7.201 | raw FV | unknown (vendor may have removed) |
| `images/fairlady-CN-16.0.7.200.img` | fairlady | CN | 16.0.7.200 | raw FV | unknown (vendor may have removed) |
| `/home/vivy/gbl_root_canoe/tests/extracted/LinuxLoader.efi` | myron | ? | ? | unwrapped PE | yes (assumed) |
| `/home/vivy/gbl_root_canoe/tests/002_infiniti_abl.elf` | infiniti | ? | (older, ELF) | ELF (not PE) | yes (assumed) |
| `/home/vivy/gbl_root_canoe/tests/001_myron_abl.elf` | myron | ? | (older, ELF) | ELF (not PE) | yes (assumed) |

The `.elf` files in `tests/` are ABL ELF binaries (pre-FV-extraction) used by gbl_root_canoe's own build. Implementation phase decides whether to use them as fixtures: they're pre-FV-extraction so the patch9 RE on them targets ELF section addressing, not PE file offsets. May be cleaner to skip the ELFs and target the unwrapped LinuxLoader.efi binaries (the `images/*.img` raw FVs each yield a LinuxLoader PE after unwrap).

**Patch1 anchor expectation:** patch1's `efisp` UTF-16LE string is part of the GBL-loader plumbing. Some newer vendor OTAs have removed GBL loader support. For those fixtures, patch1 will MISS — that's diagnostic information (the device can't be chainloaded at all), not a patch1 defect. The host CI's anchor-uniqueness test must distinguish:
- Expected MISS (no GBL on this fixture) — recorded in a fixture-metadata sidecar; not a failure.
- AMBIGUOUS (the anchor matches multiple times in the buffer) — always a failure; means the anchor isn't unique enough.
- Unexpected MISS (fixture is annotated as having GBL but anchor missed) — a failure; means anchor doesn't survive across this fixture.

**Patch9 anchor expectation:** libavb is independent of GBL loader. patch9's anchors should match OK on every fixture in the table (even those with GBL removed), because libavb is still part of ABL's verified-boot path.

### Anchor derivation strategy

- Start with a 24-32 byte anchor on each Site V / Site G candidate.
- Disassemble the relevant region in every PE-format fixture (extract LinuxLoader.efi from the raw FV `.img`/`abl.bin` files first via AblUnwrapLib's host-callable equivalent, or via a one-off `extractfv`-style tool).
- If a pattern doesn't match across all PE fixtures, shorten to 8-12 bytes with masked wildcards over the variable parts (function-prologue stack offsets typically vary; instruction opcodes are stable).
- Source-level invariants to lean on: known string constants in libavb (`"AVB0"` magic), known function-prologue patterns of called helpers (e.g. `avb_slot_verify`'s prologue), the `bl` to `avb_slot_verify` itself (its relative-call encoding shifts but the call exists in every libavb-using ABL).

### Out of scope for patch9 v2

- Substituting stock vbmeta data into the partition-read boundary (the avb-input-facade approach in `docs/re/avb-input-facade.md`). User confirmed: out of scope and out of repo unless the binary patch alone proves insufficient.
- Carrying lock/unlock state through ABL's full state machine (gbl_root_canoe-style patches 1-5). Per user: overkill since the protocol hook already fakelocks ABL's view.

## Section 5 — Build matrix update

```
GBL_MODE  AUTO  DEBUG  VERBOSE  LogFs  ProtocolHooks  Patches
─────────────────────────────────────────────────────────────────────────
0         0     0      0        on     none           patch1
0         1     0      0        on     none           patch1
0         1     1      1        on     none           patch1
1         0     0      0        on     universal+m1   patch1 + patch9 v2
1         1     0      0        on     universal+m1   patch1 + patch9 v2
1         1     1      1        on     universal+m1   patch1 + patch9 v2
```

`scripts/build.sh --mode {0|1|2|3} [--auto] [--debug] [--verbose]` — `--mode 0` is new; mode-2/3 placeholders remain (warn but compile-attempt).

Default `runall` builds:

| artifact | flags |
|---|---|
| `dist/mode-0.efi` | `--mode 0` |
| `dist/mode-0-auto-debug-verbose.efi` | `--mode 0 --auto --debug --verbose` |
| `dist/mode-1.efi` | `--mode 1` |
| `dist/mode-1-auto-debug-verbose.efi` | `--mode 1 --auto --debug --verbose` |

`010_build_smoke` covers all four.

## Section 6 — Validation sequence (per user's flow)

The full device-test path:

1. `fastboot reboot recovery` — boot into custom (TWRP-style) recovery.
2. `fastboot stage dist/mode-N{-flags}.efi` — host-stage the payload to canoe-platform fastboot's staging area.
3. `fastboot oem boot-efi` — triggers the staged EFI. The user's permanent EFISP (old auto_debug_mode) hands off to our payload.
4. (`AUTO=1` branch) Wait for FastbootLib menu → `fastboot oem escape` → `BootFlowChainLoad` runs.
   (`AUTO=0` branch) `BootFlowChainLoad` runs after the key window.
5. AblUnwrap → DynamicPatch_Apply → (mode≥1) ProtocolHook_InstallAll → LogFs proper transition (flush+close) → LoadImage → patched ABL.
6. ABL's libavb runs:
   - Mode-0: real on-disk recovery image; libavb returns OK_NOT_SIGNED with permissive flag (because ABL sees unlocked); ABL proceeds via its natural unlocked-orange path. KM/Oplus emit orange/unlocked. Custom recovery boots.
   - Mode-1: real on-disk recovery image; libavb returns OK_NOT_SIGNED (with patch9 v2's flag forcing); ABL's post-libavb gate (with patch9 v2's gate rewrite) doesn't fatal; ABL builds locked/green output (KM `0x208` `isUnlocked=0, color=0`, oplusboot.verifiedbootstate=green). Custom recovery boots.
7. `adb pull` bootloader_log + boot props from custom recovery for forensic evidence.

**Success criterion (mode-1):** custom recovery boots AND KM 0x208 wire bytes confirm `isUnlocked=0, color=0` AND oplusboot cmdline shows green.

**Success criterion (mode-0):** custom recovery boots (regardless of KM/Oplus state — mode-0 doesn't fakelock).

## Section 7 — Stop-lines

- Do not gate logfs on mode. It must work everywhere. If implementation discovers a hard architectural blocker, surface for re-spec.
- Do not change `AllowVerificationError`'s color-computation behavior. The protocol hook is the single authoritative source of GREEN/LOCKED.
- Do not patch sites that the libavb audit identified as untouchable (line 1728's color computation). Patch9 v2 must keep that line natural.
- Do not adopt gbl_root_canoe's full state-rewrite approach (patches 1-5). Per user: indirect, more rewrites than needed when a protocol hook already fakelocks.
- Do not ship patch9 v2 if it can't be validated PATCH_OK against at least 3 PE-format fixtures from the inventory in §4 (preferably spanning 2+ device codenames and 2+ OS versions per device). The full set is 4 raw-FV `.img`/`abl.bin` fixtures + 1 already-unwrapped `LinuxLoader_infiniti.efi`; after FV-unwrap during implementation we have 5 PE fixtures, of which 3+ must hit OK on patch9 with the same anchor.
- Patch1's expected MISS on no-GBL fixtures is documented in fixture metadata (a `.fixture-meta.json` sidecar or equivalent). CI tolerates that MISS; it does not tolerate AMBIGUOUS on any fixture.
- Do not push to main without a successful host runall + at least one device-test success on mode-0 before mode-1 (sequencing for diagnostic isolation).

## Section 8 — Out of scope

- Mode-2 (TA-payload spoof) — plan-2.
- Mode-3 (universal-only, leaf-survival gamble) — plan-3.
- ABL embed (build-time cache of pre-patched ABL) — plan-3.
- AVB input façade (docs/re/avb-input-facade.md) — only revisited if patch9 v2 proves insufficient at runtime.
- Userspace teesim/RKP shims.
- BlockIO write filter for `oplusreserve1`.

## Section 9 — Implementation outline

Detailed step-by-step plan goes to `docs/superpowers/plans/2026-05-09-mode0-logfs-fix-and-patch9-v2.md` (writing-plans phase).

Sequence:

1. **logfs diagnostic build (mode-0 + verbose LogFsInit).** Build a one-off, hand to user, get device evidence.
2. **logfs root-cause + fix.** Implement the minimal fix that makes logfs mount (no shims).
3. **Add LogFs proper transition** in BootFlow.c (flush+close before LoadImage).
4. **Mode-0 implementation.** GBL_MODE=0 plumbing through .dsc, .inf, PatchTable aggregator, BootFlow.c, build.sh, runall.
5. **Patch7 archive.** Comment out the entry from `kOemOneplusPatches[]`.
6. **Patch9 v2 RE.** FV-unwrap each raw `.img`/`abl.bin` fixture in `images/` (EU 16.0.5.703, IN 16.0.7.201, fairlady CN 16.0.7.200) to obtain its LinuxLoader.efi PE. Use the gbl_root_canoe `tests/extracted/LinuxLoader.efi` (myron) as another PE fixture. The old infiniti `images/infiniti/LinuxLoader_infiniti.efi` is already a PE. For each of these 5 PE fixtures: disassemble the libavb-call region in `LoadImageAndAuthVB2` (source equivalent at `VerifiedBoot.c:~1300-1700`), locate Site V (VerifyFlags derivation) and Site G (post-libavb gate). Compare the instruction sequences across all 5; derive an anchor per site that matches PATCH_OK on at least 3 of them (preferably spanning multiple device codenames and OS versions). Document expected MISS on any fixture where the libavb call site is structurally absent.
7. **Patch9 v2 implementation.** Rewrite `mode_1/mode_1.c::ApplyAvbLockedRecoverableContinue` with new Site V + Site G logic; update `tests/patches/test_patch9.c` for new expected post-bytes across all three fixtures.
8. **Build matrix update.** Verify `scripts/build.sh --mode 0` works; rebuild all default artifacts.
9. **Validation.** Stage `dist/mode-0-auto.efi` on device first (validates foundation); then stage `dist/mode-1-auto-debug-verbose.efi` (validates patch9 v2).

End-state checklist (for verifier):

- [ ] `dist/mode-0.efi` builds; chainloads through to ABL on device with patch1 OK, logfs mounted.
- [ ] `dist/mode-0-auto.efi` boots silently to ABL; custom recovery launches.
- [ ] LogFs proper transition releases the partition handle before LoadImage (no stale binding for chained EFIs).
- [ ] `dist/mode-1-auto-debug-verbose.efi` boots through to custom recovery with KM 0x208 isUnlocked=0, color=0, real OEM pubkey.
- [ ] patch9 v2 anchors match unique-and-correctly on all three fixtures (host CI green).
- [ ] No `AllowVerificationError` color-site rewrite (line 1728 untouched).
- [ ] All host tests pass; runall green.

## Open items deferred to writing-plans

- Logfs root-cause diagnosis (depends on device evidence from the diagnostic build; resolution required before mode-0 ships).
- Mihon binary path within `gbl_root_canoe/tests/` (verify during plan writing).
- Exact AArch64 instruction patterns for Site V and Site G (depends on disassembly during plan writing).
- The specific anchor pattern bytes (depend on multi-binary disassembly).
- Decision between Approach A and Approach B for patch9 v2: a data-driven selection made during the disassembly phase of plan writing. Both architectures are evaluated against all three binaries up-front; the architecture with cleaner cross-binary anchor stability wins. Implementation commits to one architecture, not both layered.
