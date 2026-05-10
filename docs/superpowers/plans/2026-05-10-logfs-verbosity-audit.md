# Logfs Verbosity & Failed-Boot Capture Audit — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** make `GblChainload_BootN.txt` reliably contain verbose triage data we'd want next time a boot fails, without flooding the small logfs partition.

**Architecture:** four sub-tasks. (1) Audit the 224-byte banner-only anomaly — find when `LogFsInstallDebugSink` runs vs when output begins, identify gaps. (2) Widen `PcdDebugPrintErrorLevel` from `0x80000042` to `0x80400042` so `DEBUG_VERBOSE` lines land in the file. (3) Targeted instrumentation pass at decision points (mode selection, swallow-vs-passthrough, VB ROT). (4) Validate from a cold boot with the (Track-0-hardened) test-device-automatic.sh, measure size + content, decide ship vs trim.

**Tech Stack:** EDK-II, `DebugLib`, `PcdDebugPrintErrorLevel`, our `LogFsLib`/`DebugSink.c`/`Mount.c`, `BootFlow.c`.

**Depends on:** Track 0 (`2026-05-10-test-device-hardening.md`) merged first so test runs are reliable.

**File structure:**
- `docs/re/logfs-capture-gaps.md` — new findings doc from Task 1.
- `GblChainloadPkg/GblChainloadPkg.dsc` — PCD change in Task 2.
- `GblChainloadPkg/Application/GblChainload/BootFlow.c` — instrumentation Task 3.
- `GblChainloadPkg/Library/ProtocolHookLib/ScmHook.c` — instrumentation Task 3 (already has VERBOSE; verify lines land).
- `GblChainloadPkg/Library/ProtocolHookLib/QseecomHook.c` — same.
- `GblChainloadPkg/Library/ProtocolHookLib/SpssHook.c` — same.
- `GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c` — instrumentation Task 3.
- `GblChainloadPkg/Library/LogFsLib/DebugSink.c` — only if Task 1 reveals a wiring bug.

---

## Task 1: Audit — find why `GblChainload_Boot1.txt` was 224 bytes

**Files:**
- Inspect: `GblChainloadPkg/Application/GblChainload/Entry.c`
- Inspect: `GblChainloadPkg/Application/GblChainload/BootFlow.c`
- Inspect: `GblChainloadPkg/Library/LogFsLib/DebugSink.c`
- Inspect: `GblChainloadPkg/Library/LogFsLib/Mount.c`
- Create: `docs/re/logfs-capture-gaps.md`

The most recent system-context capture had `GblChainload_Boot{1,2}.txt` = 224 bytes (banner-only). Earlier recovery captures had ~600 bytes. Find the path-dependent gap.

- [ ] **Step 1: Locate every DebugSink install/remove site**

```bash
cd /home/vivy/gbl-chainload
grep -nE "LogFsInstallDebugSink|LogFsRemoveDebugSink" GblChainloadPkg/Application/GblChainload/*.c GblChainloadPkg/Library/LogFsLib/*.c
```

Expected: at least two install sites (`Entry.c:136`, `BootFlow.c:74`) and at least one remove site (`Entry.c:148` likely).

- [ ] **Step 2: Read each install site and its surrounding context**

For each match, read the function it's in. Note:
- When does the install happen relative to mounting the logfs?
- What `Print(...)` / `DEBUG(...)` calls happen *before* the install on that code path?
- Are there code paths that reach `BootFlow` without going through `Entry`'s install?

Use:
```bash
sed -n '120,160p' /home/vivy/gbl-chainload/GblChainloadPkg/Application/GblChainload/Entry.c
sed -n '60,100p' /home/vivy/gbl-chainload/GblChainloadPkg/Application/GblChainload/BootFlow.c
```

- [ ] **Step 3: Inventory log slots from the most recent system-context capture**

```bash
ls -la /home/vivy/gbl-chainload/logs/20260510-150754_manual_manual_v2.0-plan1/logfs/
head -5 /home/vivy/gbl-chainload/logs/20260510-150754_manual_manual_v2.0-plan1/logfs/GblChainload_Boot1.txt
wc -c /home/vivy/gbl-chainload/logs/20260510-150754_manual_manual_v2.0-plan1/logfs/GblChainload_Boot*.txt
```

Expected: file sizes of `GblChainload_Boot{1,2}.txt` confirmed at 224 bytes. Heads show only the banner (`=== gbl-chainload - mode-1 - ...`).

- [ ] **Step 4: Compare with the recovery-context capture from the prior session**

```bash
ls /home/vivy/gbl-chainload-dirty/logs/ | grep -iE "fakelocked" | head -3
```

Pick one recovery-context dir from `~/gbl-chainload-dirty/logs/` (e.g. `20260508-202149_manual_mode-fakelocked-stock-images_vf91f6f3`). Compare its `logfs/GblChainload_Boot*.txt` content.

```bash
DIRTY_DIR=$(ls -d ~/gbl-chainload-dirty/logs/20260508-202149_manual_mode-fakelocked-stock-images_vf91f6f3 2>/dev/null || ls -d ~/gbl-chainload-dirty/logs/*manual_mode-fakelocked-stock-images* | head -1)
echo "comparing against: $DIRTY_DIR"
ls -la "$DIRTY_DIR/logfs/" 2>&1 | tail
head -20 "$DIRTY_DIR/logfs/GblChainload_Boot1.txt" 2>/dev/null
```

Note: how much content does the recovery capture have? What's the difference in paths taken?

- [ ] **Step 5: Write findings**

Create `docs/re/logfs-capture-gaps.md`:

```markdown
# Logfs capture gaps — 2026-05-10

## Symptom

`GblChainload_Boot{1,2}.txt` in system-context capture
(`logs/20260510-150754_manual_manual_v2.0-plan1`) = 224 bytes
(banner-only). Recovery-context captures from earlier sessions show
~600 bytes with structured BootFlow/AblUnwrap/DynamicPatch output.

## DebugSink install timing

(Cite file:line for each install/remove. Describe the code paths.)

## Output emitted before DebugSink installs

(Find Print/DEBUG calls earlier in Entry/BootFlow flow. Cite file:line.)

## Hypotheses

(One of, with evidence)
- DebugSink installs after key BootFlow Prints — output goes to ConOut
  before the sink hooks. Fix: install sink earlier.
- The "224-byte" boot path doesn't run BootFlow at all (e.g. autoboot
  fastbootlib path skips it). Confirm by checking what banner the
  Boot1 slot has and which mode it identifies.
- DebugSink installs but `LogFsRemoveDebugSink` fires too early on
  this path.
- ConOut bypassed (some path uses raw SerialPortWrite or similar).

## Recommendation

(Specific actionable change: move install earlier / add install at
path X / fix early remove / etc.)
```

- [ ] **Step 6: Commit**

```bash
cd /home/vivy/gbl-chainload
git add docs/re/logfs-capture-gaps.md
git commit -m "Audit: GblChainload_BootN.txt 224-byte anomaly findings"
```

---

## Task 2: Widen `PcdDebugPrintErrorLevel` to include `DEBUG_VERBOSE`

**Files:**
- Modify: `GblChainloadPkg/GblChainloadPkg.dsc`

- [ ] **Step 1: Confirm current PCD**

```bash
cd /home/vivy/gbl-chainload
grep -n "PcdDebugPrintErrorLevel" GblChainloadPkg/GblChainloadPkg.dsc
```

Expected: line near 169, value `0x80000042` (ERROR + WARN + INFO).

- [ ] **Step 2: Change the value**

Edit `GblChainloadPkg/GblChainloadPkg.dsc`. Find:

```
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042
```

Replace with:

```
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80400042
```

(Added bit `0x00400000` = `DEBUG_VERBOSE`.)

- [ ] **Step 3: Build**

```bash
cd /home/vivy/gbl-chainload && ./scripts/build.sh --mode 1 --auto --debug --verbose 2>&1 | tail -10
```

Expected: clean build. Note the size of `dist/mode-1-auto-debug-verbose.efi` for the size-delta record.

- [ ] **Step 4: Sanity check the bit interpretation**

Reference: EDK-II `Base.h` `DEBUG_*` masks:
```
DEBUG_ERROR    0x80000000
DEBUG_WARN     0x00000002
DEBUG_INFO     0x00000040
DEBUG_VERBOSE  0x00400000
```

`0x80000042` = ERROR + WARN + INFO. `0x80400042` = same + VERBOSE. Confirm by:

```bash
python3 -c "print(hex(0x80000042 | 0x00400000))"
```

Expected: `0x80400042`.

- [ ] **Step 5: Commit**

```bash
cd /home/vivy/gbl-chainload
git add GblChainloadPkg/GblChainloadPkg.dsc
git commit -m "DSC: widen PcdDebugPrintErrorLevel to include DEBUG_VERBOSE"
```

---

## Task 3: Apply Task 1 fix (DebugSink wiring)

**Files:**
- Modify: `GblChainloadPkg/Application/GblChainload/Entry.c` OR
- Modify: `GblChainloadPkg/Application/GblChainload/BootFlow.c` OR
- Modify: `GblChainloadPkg/Library/LogFsLib/DebugSink.c`

(Whichever file Task 1's recommendation in `docs/re/logfs-capture-gaps.md` points to.)

If Task 1's recommendation is "no fix needed — 224-byte was a benign autoboot path that doesn't reach BootFlow", skip directly to Step 5 (mark this task done with no change).

- [ ] **Step 1: Re-read the findings**

```bash
cat /home/vivy/gbl-chainload/docs/re/logfs-capture-gaps.md
```

The "Recommendation" section names the specific change. Common shapes:

**(A) Move DebugSink install earlier.** If install currently happens at
`Entry.c:136` after several Print calls, move it to the first line
after `gST` and `gBS` are usable. Insert near the very top of `EfiMain`:

```c
/* Hook ConOut as early as possible — anything Print()ed after this
   line mirrors into the post-GBL log slot once mounted. */
LogFsInstallDebugSink ();
```

(Note: install before logfs is mounted means `LogFsIsReady()` returns
false and DebugSink's mirror branch is skipped. That's OK — it queues
when ready. If LogFsLib doesn't buffer, the "miss early Prints" gap
remains a known limitation; document it in the findings.)

**(B) Add install at a missing path.** If BootFlow has an entry from a
mode that doesn't call install, add it.

**(C) Fix early `LogFsRemoveDebugSink`.** Remove the premature uninstall.

- [ ] **Step 2: Apply the change**

Edit the named file per the recommendation. Show the exact diff in
this step's commit message.

- [ ] **Step 3: Build**

```bash
cd /home/vivy/gbl-chainload && ./scripts/build.sh --mode 1 --auto --debug --verbose 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 4: Smoke test (host)**

If `tests/runall.sh` exists and includes anything that exercises
LogFsLib host shims:

```bash
cd /home/vivy/gbl-chainload && bash tests/runall.sh 2>&1 | tail -10
```

Expected: ALL TESTS PASS (or "no host tests for LogFsLib" — that's OK).

- [ ] **Step 5: Commit**

```bash
cd /home/vivy/gbl-chainload
git add GblChainloadPkg/
git commit -m "LogFsLib/BootFlow: <specific change from findings doc>"
```

If no change made: `git commit --allow-empty -m "LogFsLib: audit confirms current install timing OK"`.

---

## Task 4: Add `BootFlow:` mode-selection decision logging

**Files:**
- Modify: `GblChainloadPkg/Application/GblChainload/BootFlow.c`

When BootFlow picks a mode branch (mode-0/1/fakelocked/FastbootLib),
emit a `Print` line naming the chosen branch and the reason. This is
essential for "which path did we actually take" triage.

- [ ] **Step 1: Locate the mode-decision switch in BootFlow.c**

```bash
cd /home/vivy/gbl-chainload
grep -nE "GBL_MODE|switch.*mode|case 0|case 1|fakelocked" GblChainloadPkg/Application/GblChainload/BootFlow.c | head -20
```

Find the switch/if-else that picks the boot branch (mode-0 vs mode-1
vs fakelocked vs entering FastbootLib).

- [ ] **Step 2: Add a Print at each branch entry**

For each branch, add one line right after the branch is entered:

```c
Print (L"BootFlow: branch=mode-1 reason=key-window-not-pressed\n");
```

Adapt the `reason=` to what the actual condition tested for that
branch. Common reasons:
- `key-window-pressed` (volume up held)
- `key-window-not-pressed`
- `mode-token=N` (whatever the build-time mode pin is)
- `fastboot-fallback`

Example concrete insertions (adapt to actual code structure):

```c
/* mode-0 branch */
Print (L"BootFlow: branch=mode-0 reason=baseline-build\n");

/* mode-1 branch */
Print (L"BootFlow: branch=mode-1 reason=fakelocked-via-patches\n");

/* fastbootlib auto-entry branch */
Print (L"BootFlow: branch=fastbootlib reason=auto-debug-build\n");
```

- [ ] **Step 3: Build**

```bash
cd /home/vivy/gbl-chainload && ./scripts/build.sh --mode 1 --auto --debug --verbose 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
cd /home/vivy/gbl-chainload
git add GblChainloadPkg/Application/GblChainload/BootFlow.c
git commit -m "BootFlow: log chosen mode branch + reason"
```

---

## Task 5: Audit hook verbose-line presence

**Files:**
- Inspect: `GblChainloadPkg/Library/ProtocolHookLib/ScmHook.c`
- Inspect: `GblChainloadPkg/Library/ProtocolHookLib/QseecomHook.c`
- Inspect: `GblChainloadPkg/Library/ProtocolHookLib/SpssHook.c`
- Inspect: `GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c`

The spec said existing hooks already have `DEBUG((DEBUG_VERBOSE, ...))`
lines. After Task 2's PCD widening those should land in the file. Verify
they exist; if they don't, add minimal ones.

- [ ] **Step 1: Count DEBUG_VERBOSE callsites per hook**

```bash
cd /home/vivy/gbl-chainload
for f in GblChainloadPkg/Library/ProtocolHookLib/{Scm,Qseecom,Spss,VerifiedBoot}Hook.c; do
  echo -n "$f: "
  grep -c "DEBUG_VERBOSE" "$f"
done
```

Note the counts. If ALL are non-zero: Task 5 ends here (existing
VERBOSE coverage is sufficient).

- [ ] **Step 2: For any hook with zero `DEBUG_VERBOSE` lines, add one per intercept slot**

Pattern, applied to each missing hook. Example for a hook slot named
`HookedScmSipSysCall`:

```c
/* (already present): log line on first-entry first-pass */
Print (L"ScmHook: SipSysCall Cmd=0x%x\n", Cmd);

/* New: VERBOSE line dumping payload prefix and result code, gated by
   the same reentry guard. */
DEBUG ((DEBUG_VERBOSE,
        "ScmHook.SipSysCall: cmd=0x%x args[0..3]=%016lx %016lx %016lx %016lx => %r\n",
        Cmd, Arg0, Arg1, Arg2, Arg3, Status));
```

Add one per primary intercept entry-point. Don't go overboard — one
DEBUG_VERBOSE per hooked slot is enough; Task 2's PCD widening makes
them visible.

- [ ] **Step 3: Build**

```bash
cd /home/vivy/gbl-chainload && ./scripts/build.sh --mode 1 --auto --debug --verbose 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 4: Commit (only if changes made)**

```bash
cd /home/vivy/gbl-chainload
git add GblChainloadPkg/Library/ProtocolHookLib/
git commit -m "ProtocolHookLib: add minimal DEBUG_VERBOSE coverage at each intercept slot"
```

If counts were all non-zero: `git commit --allow-empty -m "ProtocolHookLib: audit confirms DEBUG_VERBOSE coverage adequate"`.

---

## Task 6: Validate from a cold boot

**Files:**
- None modified. Validation gate.

Requires the device.

- [ ] **Step 1: Confirm Track 0 hardening is merged**

```bash
cd /home/vivy/gbl-chainload
git log --oneline -10 | grep -E "test-device|Track 0|hardening"
```

Expected: at least one commit landing the Track 0 plan's changes. If
not, run Track 0 first.

- [ ] **Step 2: Power off device. Boot it into bootloader fastboot.**

```bash
fastboot devices
```

Expected: one fastboot device.

- [ ] **Step 3: Run the hardened automatic test**

```bash
cd /home/vivy/gbl-chainload
./scripts/test-device-automatic.sh 2>&1 | tee /tmp/logfs-verbose-validation.log
```

- [ ] **Step 4: Locate the new log dir and inspect Boot*.txt sizes**

```bash
LATEST=$(ls -td logs/*/ | head -1)
echo "Latest: $LATEST"
ls -lh "$LATEST/logfs/" 2>&1 | tail
```

Expected:
- At least one `GblChainload_BootN.txt` significantly larger than 224
  bytes (target: > 1 KB; ideally 5–20 KB for an active boot path).
- Total `GblChainload_Boot*.txt` summed across all 5 slots < 100 KB.
- Per-boot < 50 KB.

- [ ] **Step 5: Spot-check content quality**

```bash
head -40 "$LATEST/logfs/GblChainload_Boot1.txt"
```

Verify the output contains:
- `BootFlow: branch=` line from Task 4
- ScmHook / QseecomHook / VerifiedBootHook lines (some VERBOSE)
- Per-patch result line(s) ("DynamicPatch: patchN-... -> OK")
- ProtocolHookLib install summary

If content is sparse or missing, return to Task 1's findings and add
the missing instrumentation in a follow-up commit.

- [ ] **Step 6: Decision**

- Content useful AND per-boot < 50 KB: **ship**. Continue to Step 7.
- Too noisy (any slot > 50 KB): trim instrumentation OR revert Task 2's
  PCD widening (drop DEBUG_VERBOSE bit). Return to Step 3 after the trim.
- Content still sparse: open follow-up commit adjusting Task 4/5
  instrumentation. Don't iterate forever — if Step 5 fails after 2
  trims, ship PCD-only and document remaining gaps.

- [ ] **Step 7: Final commit**

```bash
cd /home/vivy/gbl-chainload
git add -u
git commit --allow-empty -m "Track 1: logfs verbosity validated against cold boot
  
  Validated against test-device-automatic.sh:
  - per-boot size: <fill in>
  - Boot1.txt content lines: <fill in>
  - PCD = 0x80400042 (ERROR+WARN+INFO+VERBOSE)
  - BootFlow mode-branch logging active"
```

---

## Self-Review

Checked against `docs/superpowers/specs/2026-05-10-logfs-verbosity-audit-design.md`:

- ✓ Sub-task 1 (audit & repro) → Task 1.
- ✓ Sub-task 2 (PCD widen) → Task 2.
- ✓ Sub-task 3 (targeted instrumentation pass) → Task 4 (BootFlow mode-branch logging) + Task 5 (hook DEBUG_VERBOSE audit).
- ✓ Validation flow (acceptance criteria, trim path) → Task 6.

No placeholders. Type consistency: `LogFsInstallDebugSink`, `LogFsIsReady`, `LogFsRemoveDebugSink` names are uniform across Task 1 (audit) and Task 3 (fix). PCD value `0x80400042` consistent in Tasks 2 + 6's commit message. Mode-branch reason strings (`key-window-pressed`, etc.) are consistent in Task 4.

One spec item explicitly NOT given a task: per-patch attempt logging in
DynamicPatchLib. The spec says "already logged but verify". Task 6 Step 5
spot-checks for it as part of content quality — if missing, a follow-up
commit can add it; not enough surface to deserve a dedicated task.
