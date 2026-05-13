# Cleanup Phase 1 — PR-b: Synth/Graft Regression Guard Implementation Plan (v2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lock in the synth/graft removal that PR #13 already accomplished. Add a host-side regression test that fails if any of the three abandoned command literals (`synthesize-and-flash`, `graft-from-staged`, `fix-vbmeta-footer`) reappear in code or scripts. Mark the two related memory notes as historical so future readers know the artifacts they describe no longer exist on `main`.

**Architecture:** Single small PR. One feature branch off post-revert `main`. Three changes total: create a test script, add it to `runall.sh`, prepend a banner to two memory notes. No code changes outside the test itself. No edk2 work.

**Tech Stack:** Bash (test script), Markdown (memory notes).

**Spec reference:** `docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md` (v2 after PR-#8 revert), section "PR-b: Regression guard for the synth/graft surface (P2)".

**v1 → v2 diff:** Dropped all edk2 work (the on-device synth/graft surface lives only on an orphan edk2 submodule branch never pulled into main's pointer; nothing to delete from main's perspective). Dropped the `scripts/synthesize-vbmeta.py` deletion (PR #13 already did that). Slimmed PR-b to its forward-guard purpose only.

---

## File Structure

- **Create:** `tests/050_no_synth_graft_surface.sh` — host-side regression check.
- **Modify:** `tests/runall.sh` — add one line invoking the new test.
- **Modify:** `memory/graft_at_natural_offset_wins.md` — prepend historical banner.
- **Modify:** `memory/avb_constructed_verify_blocked.md` — prepend historical banner.

---

## Task 1: Create feature branch

**Files:** none.

- [ ] **Step 1: Sync main**

Run:
```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git log --oneline -3
```

Expected: HEAD shows the PR #13 revert merge (post-revert main; e.g. `061a20d`). `ls scripts/synthesize-vbmeta.py 2>&1` returns "No such file or directory" — confirming the revert is in.

- [ ] **Step 2: Branch off main**

Run:
```bash
git checkout -b feature/cleanup-p1b-regression-guard
```

---

## Task 2: Write the regression test (TDD red phase first)

**Files:** Create: `tests/050_no_synth_graft_surface.sh`.

- [ ] **Step 1: Inspect existing test conventions**

Run: `head -20 tests/042_dynamic_patch_harness.sh tests/045_mode_taxonomy_lint.sh 2>&1 | head -40`

Expected: shell tests with `#!/usr/bin/env bash` shebang and `set -euo pipefail` preamble. Match that style.

- [ ] **Step 2: Author the test**

Create `tests/050_no_synth_graft_surface.sh` with this content:

```bash
#!/usr/bin/env bash
# 050_no_synth_graft_surface.sh — regression check.
#
# The synth/graft on-device fastboot surface (oem synthesize-and-flash,
# oem graft-from-staged, oem fix-vbmeta-footer) was an experiment that
# never landed on main. The host helper that fed it (scripts/synthesize-
# vbmeta.py) was reverted from main by PR #13 on 2026-05-12.
#
# This test asserts none of those three command literals reappear in
# code or scripts. Docs, memory notes, and .re-notes/ retain the names
# as historical references — those locations are excluded.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

NEEDLES=(synthesize-and-flash graft-from-staged fix-vbmeta-footer)

# Search scope: code-bearing directories. Exclude docs, memory, .re-notes,
# the test itself, and the gitignored top-level images/ tree.
SCOPES=(
  "$ROOT/GblChainloadPkg"
  "$ROOT/edk2/QcomModulePkg/Library/FastbootLib"
  "$ROOT/scripts"
  "$ROOT/tools"
)

fail=0
for needle in "${NEEDLES[@]}"; do
  hits=""
  for scope in "${SCOPES[@]}"; do
    [ -d "$scope" ] || continue
    found=$(rg -n --no-heading -- "$needle" "$scope" 2>/dev/null || true)
    if [ -n "$found" ]; then
      hits="${hits}${found}"$'\n'
    fi
  done
  if [ -n "$hits" ]; then
    echo "FAIL: '$needle' present in code paths:" >&2
    echo "$hits" >&2
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "OK: synthesize/graft surface absent from code paths."
```

- [ ] **Step 3: Make it executable**

Run: `chmod +x tests/050_no_synth_graft_surface.sh`

- [ ] **Step 4: Run the test — expect PASS on post-revert main**

Run: `tests/050_no_synth_graft_surface.sh`

Expected: exit code 0, output `OK: synthesize/graft surface absent from code paths.`

If FAIL: the revert (PR #13) didn't fully clean main. Capture the output, STOP, and escalate — main is in an unexpected state.

(NOTE: This isn't classical TDD red/green because the system under test — the revert — already happened. This test is a forward-guard, so it must PASS from the moment it's written. If you want a deliberate red-phase verification, temporarily plant `synthesize-and-flash` in a scratch file under `scripts/`, run, confirm FAIL, remove the scratch file, run again, confirm PASS. Optional.)

---

## Task 3: Wire the test into runall.sh

**Files:** Modify: `tests/runall.sh`.

- [ ] **Step 1: Read current runall.sh**

Run: `cat tests/runall.sh`

Expected: a sequence of `echo "== NNN_name ==" && bash tests/NNN_name.sh` invocations in numeric order. Locate the insertion point for `050_no_synth_graft_surface.sh` — between any test with a number ≤ 047 and any test with a number ≥ 051.

The likely candidates (post-revert) are: `042_dynamic_patch_harness`, `045_mode_taxonomy_lint`, `046_mode1_protocol_hook_lint`, `047_cleanup_lint`, `051_gbl_root_canoe_regression`. The new test slots between `047_cleanup_lint` and `051_gbl_root_canoe_regression`.

- [ ] **Step 2: Insert the entry**

After the lines:

```bash
echo "== 047_cleanup_lint =="
bash tests/047_cleanup_lint.sh
```

Insert:

```bash
echo "== 050_no_synth_graft_surface =="
bash tests/050_no_synth_graft_surface.sh
```

- [ ] **Step 3: Verify**

Run: `grep -n '050_no_synth_graft_surface' tests/runall.sh`

Expected: exactly two hits — the `echo` line and the `bash` line — in numeric order with the other tests.

- [ ] **Step 4: Run the full suite to confirm no regression**

Run: `tests/runall.sh`

Expected: completes with `ALL TESTS PASS`. If it FAILs on a test other than `050_no_synth_graft_surface`, that's a pre-existing condition you should NOT try to fix in this PR — STOP and escalate. If `050` is the one failing, return to Task 2 Step 4.

---

## Task 4: Add historical banner to graft_at_natural_offset_wins.md

**Files:** Modify: `memory/graft_at_natural_offset_wins.md`.

- [ ] **Step 1: Read top of file**

Run: `head -15 memory/graft_at_natural_offset_wins.md`

Expected: YAML frontmatter delimited by `---` lines, then the memory body.

- [ ] **Step 2: Insert banner after the frontmatter, before the body**

Find the closing `---` of the YAML frontmatter (the second `---` from the top). Insert a blockquote on the line immediately after, with a trailing blank line:

```markdown
> **Historical (2026-05-12):** Technique confirmed on infiniti during the abandoned `feature/synthesize-fastboot-cmd` exploration. The on-device fastboot surface never landed on `main`; PR #13 reverted the stranded host helper. Captured here for whoever rebuilds the host script + on-device module under Cleanup Phase 2.

```

- [ ] **Step 3: Verify frontmatter intact**

Run: `head -12 memory/graft_at_natural_offset_wins.md`

Expected: YAML frontmatter at top (opens with `---`, closes with `---`), banner blockquote immediately after, then memory body. The frontmatter MUST remain valid (key:value pairs unchanged).

---

## Task 5: Add historical banner to avb_constructed_verify_blocked.md

**Files:** Modify: `memory/avb_constructed_verify_blocked.md`.

- [ ] **Step 1: Read top of file**

Run: `head -15 memory/avb_constructed_verify_blocked.md`

- [ ] **Step 2: Insert banner after frontmatter**

Same pattern as Task 4. Insert:

```markdown
> **Historical (2026-05-12):** Resolved in spirit by the disk-side graft path (see `graft_at_natural_offset_wins.md`); kept here for context on why constructed verify was abandoned.

```

- [ ] **Step 3: Verify both banners present**

Run: `rg -n 'Historical \(2026-05-12\)' memory/`

Expected: exactly two hits — one in `graft_at_natural_offset_wins.md`, one in `avb_constructed_verify_blocked.md`.

---

## Task 6: Final acceptance checks

**Files:** none (verification only).

- [ ] **Step 1: Regression test passes**

Run: `tests/050_no_synth_graft_surface.sh`

Expected: exit 0, `OK: ...`.

- [ ] **Step 2: runall.sh wires it in**

Run: `tests/runall.sh 2>&1 | grep '050_no_synth_graft_surface'`

Expected: at least the `== 050_no_synth_graft_surface ==` echo line appears in the captured output.

- [ ] **Step 3: Both banners present**

Run: `rg 'Historical \(2026-05-12\)' memory/`

Expected: exactly two hits.

- [ ] **Step 4: No accidental code changes outside the four files**

Run: `git status -s`

Expected:
- `?? tests/050_no_synth_graft_surface.sh`
- ` M tests/runall.sh`
- ` M memory/graft_at_natural_offset_wins.md`
- ` M memory/avb_constructed_verify_blocked.md`

…and possibly ` M edk2` (submodule pointer, pre-existing, unrelated — do not stage it).

---

## Task 7: Stage and commit

**Files:** stage and commit on `feature/cleanup-p1b-regression-guard`.

- [ ] **Step 1: Stage the four relevant files**

Run:
```bash
git add tests/050_no_synth_graft_surface.sh tests/runall.sh memory/graft_at_natural_offset_wins.md memory/avb_constructed_verify_blocked.md
git status -s
```

Expected: `A `, ` M`, ` M`, ` M` in the index column for the four files. `edk2` should remain unstaged. If any other file shows staged, unstage with `git restore --staged <file>` and investigate.

- [ ] **Step 2: Commit**

Run:
```bash
git commit -m "$(cat <<'EOF'
tests + memory: lock in synth/graft removal (PR-b v2 of cleanup-phase-1)

PR #13 reverted the synthesize-vbmeta host tool that fed an on-device
fastboot path which itself never landed on main. The three command
literals from the abandoned trajectory (oem synthesize-and-flash,
oem graft-from-staged, oem fix-vbmeta-footer) are gone from main's
surface.

This PR forward-guards that state and grounds the historical record:

- tests/050_no_synth_graft_surface.sh — host-side regression check.
  Greps GblChainloadPkg/, edk2/QcomModulePkg/Library/FastbootLib/,
  scripts/, and tools/ for the three command literals. Exits non-zero
  if any reappears. Wired into tests/runall.sh in numeric order.

- memory/graft_at_natural_offset_wins.md — historical banner pointing
  at PR #13 and Cleanup Phase 2 as the home for the rebuilt tooling.
- memory/avb_constructed_verify_blocked.md — historical banner
  pointing at graft_at_natural_offset_wins.md as the surviving fix.

No edk2 work. No script deletion (revert handled that). No DynamicPatchLib
or protocol-hook changes.

Refs: docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md (v2,
post-revert of PR #8 via PR #13).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify commit**

Run: `git log --oneline -1 && git show --stat HEAD`

Expected: one new commit on `feature/cleanup-p1b-regression-guard` touching exactly four files: the new test, runall.sh, and the two memory notes.

---

## Task 8: Push and open PR

**Files:** none.

- [ ] **Step 1: Push**

Run:
```bash
git push -u origin feature/cleanup-p1b-regression-guard
```

- [ ] **Step 2: Open PR**

```bash
gh pr create --title "tests+memory: lock in synth/graft removal (PR-b v2 of cleanup-phase-1)" --body "$(cat <<'EOF'
## Summary
- Adds `tests/050_no_synth_graft_surface.sh` — host-side regression check that fails if any of the three abandoned command literals (`synthesize-and-flash`, `graft-from-staged`, `fix-vbmeta-footer`) reappear in `GblChainloadPkg/`, `edk2/.../FastbootLib/`, `scripts/`, or `tools/`.
- Wires the test into `tests/runall.sh` in numeric order (between 047 and 051).
- Adds a "Historical (2026-05-12)" banner to `memory/graft_at_natural_offset_wins.md` and `memory/avb_constructed_verify_blocked.md` pointing future readers at Cleanup Phase 2.

No edk2 changes. No script deletions (PR #13 already handled `scripts/synthesize-vbmeta.py`).

Refs: `docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md` (v2). Sibling PRs: cleanup-p1a recovery doc, cleanup-p1c log stream split.

## Test plan
- [ ] `tests/050_no_synth_graft_surface.sh` exits 0 against post-merge main.
- [ ] `tests/runall.sh` runs to `ALL TESTS PASS`.
- [ ] `rg 'Historical \(2026-05-12\)' memory/` returns exactly two hits.
- [ ] Frontmatter on both memory notes still parses (open each, eyeball YAML).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 3: Confirm PR is open**

Run: `gh pr view`

Expected: base = `main`, head = `feature/cleanup-p1b-regression-guard`. Note the PR URL.
