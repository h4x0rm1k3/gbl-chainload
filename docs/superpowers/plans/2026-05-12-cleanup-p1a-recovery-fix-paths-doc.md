# Cleanup Phase 1 — PR-a: Recovery Fix-Paths Doc Implementation Plan (v2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the recovery investigation note (`docs/re/aosp-early-avb-bootflow.md`) to an authoritative status doc named `recovery-normal-boot-fix-paths.md`. Prepend two intended fix paths (both Phase-2 work). Add a "Historical — resolved" callout above the legacy Action Items section so readers don't mistake them for live work. Cross-link from README and the AVB-input-facade doc.

**Architecture:** Doc-only PR. One feature branch off post-revert `main`, one commit (file rename + content edits + two cross-link edits + one callout), one PR. No code touched. Acceptance verified by `rg` for dangling references and a manual read of the new file.

**Tech Stack:** Markdown only. `mv` (not `git mv`) for the rename — the source file is untracked in the working tree.

**Spec reference:** `docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md` (v2 after PR-#8 revert), section "PR-a: Recovery-failure decision doc (P1)".

**v1 → v2 diff:** Removed all references to `scripts/graft-vbmeta-from-stock.py` as "already exists" (the script is not on main; it's Phase-2 work). Added Task 7 for the "Historical — resolved" callout above legacy Action Items. Removed a self-defeating `rg aosp-early-avb-bootflow` test-plan checkbox (the old filename never existed on main, so the check is trivially satisfied).

---

## File Structure

- **Rename + edit:** `docs/re/aosp-early-avb-bootflow.md` → `docs/re/recovery-normal-boot-fix-paths.md` (currently untracked in working tree; one rename-and-stage operation).
- **Edit:** `README.md` (add one sentence + link to the new path).
- **Edit:** `docs/re/avb-input-facade.md` (prepend a status banner pointing at the new doc).
- **No code touched.**

---

## Task 1: Create feature branch

**Files:** none.

- [ ] **Step 1: Sync main and verify post-revert state**

Run:
```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git log --oneline -3
```

Expected: HEAD shows the PR #13 revert merge (`061a20d` or whatever the post-revert tip is). `ls scripts/` should NOT show `synthesize-vbmeta.py` or `graft-vbmeta-from-stock.py`. If either is present, STOP — main is not at the post-revert state.

- [ ] **Step 2: Branch off main**

Run:
```bash
git checkout -b feature/cleanup-p1a-recovery-fix-paths-doc-v2
```

- [ ] **Step 3: Verify source doc present in working tree**

Run: `test -f docs/re/aosp-early-avb-bootflow.md && wc -l docs/re/aosp-early-avb-bootflow.md`

Expected: file exists, ~296 lines. If absent, STOP and escalate — the doc was authored locally and must still be in the working tree for the rename.

---

## Task 2: Rename and add status banner

**Files:**
- Move: `docs/re/aosp-early-avb-bootflow.md` → `docs/re/recovery-normal-boot-fix-paths.md`
- Modify: top of the renamed file (prepend status banner above existing H1).

- [ ] **Step 1: Move the file**

Run: `mv docs/re/aosp-early-avb-bootflow.md docs/re/recovery-normal-boot-fix-paths.md`

(Plain `mv`, not `git mv`, because the file is untracked.)

Verify: `test -f docs/re/recovery-normal-boot-fix-paths.md && test ! -f docs/re/aosp-early-avb-bootflow.md`.

- [ ] **Step 2: Replace the existing title block with the new banner**

The current first ~4 lines of the moved file are:

```markdown
# AOSP Early-Stage AVB Bootflow Analysis

**Investigation Date:** 2026-05-10
**Scenario:** gbl-chainload mode-1 + patch9 v2 boots custom recovery but fails normal-boot into Android system when custom recovery is flashed alongside LKM-patched init_boot.
```

Replace those first 4 lines with:

```markdown
# Recovery Normal-Boot Fix Paths

**Status (2026-05-12):** Confirmed. Custom-recovery + normal-boot under mode-1 fails in AOSP `first_stage_init` (libfs_avb), which re-walks the vbmeta descriptor tree from disk after our shim has already lied to ABL's KM. The fix has to re-shape the on-disk recovery image *before* first-stage init reads it. Two intended paths, **both Phase-2 work**: a host-side `scripts/graft-vbmeta-from-stock.py` and a device-side recovery-graft companion module. The bootloader shim does not carry the fix because the read path it would need to intercept lives in userspace libfs_avb, outside our chainload domain. The graft technique was validated on infiniti during exploration; technique is preserved in `memory/graft_at_natural_offset_wins.md` until Phase 2 rebuilds the tooling.

**Original investigation date:** 2026-05-10.
**Scenario:** gbl-chainload mode-1 + patch9 v2 boots custom recovery but fails normal-boot into Android system when custom recovery is flashed alongside LKM-patched init_boot.
```

- [ ] **Step 3: Verify**

Run: `head -10 docs/re/recovery-normal-boot-fix-paths.md`

Expected: H1 is `# Recovery Normal-Boot Fix Paths`, immediately followed by the Status banner, then "Original investigation date" + "Scenario" lines.

---

## Task 3: Add the "Intended Fix Paths (Phase 2)" section

**Files:** Modify: `docs/re/recovery-normal-boot-fix-paths.md` (insert new section above existing "Recommended Mitigations").

- [ ] **Step 1: Locate the insertion point**

The existing doc has a section starting with:

```markdown
## Recommended Mitigations (Ranked by Likelihood)
```

Find the `---` horizontal rule **immediately preceding** that heading. Insert the new section between the previous content and that `---` rule.

- [ ] **Step 2: Insert the section**

Add the following block, ending with `---` so the existing "Recommended Mitigations" section retains its leading rule. **Both fix paths are Phase 2; neither ships in this PR.**

````markdown
---

## Intended Fix Paths (Phase 2)

Both fix paths below are Phase-2 work. Neither ships in this PR; neither lives on `main` today. The "Recommended Mitigations" section below is preserved for historical context — those options were considered as alternatives.

### Host path (Phase 2)

A `scripts/graft-vbmeta-from-stock.py` of the form:

```bash
scripts/graft-vbmeta-from-stock.py \
  --partition recovery \
  --in <custom>.img \
  --stock <stock-recovery>.img \
  --out <patched>.img
```

Writes stock recovery vbmeta at `round_up(custom_image_size, 4 KiB)`. With `patch10` in the boot path, ABL emits `verify_result_local=OK` for recovery and the slot-level recoverable error is caught by `patch10` → final OK. Technique validated on infiniti during the abandoned `feature/synthesize-fastboot-cmd` exploration; preserved in `memory/graft_at_natural_offset_wins.md`. Script TBD under Cleanup Phase 2.

### Device path (Phase 2)

On-device companion module that performs the same graft against the on-device stock vbmeta, automatically, post-recovery-flash. Lives next to the OTA-cached-ABL module. Spec TBD.
````

(Note: nest the inner ```bash fence inside the outer four-backtick fence so it renders as a code block. If your editor handles fence nesting differently, prefer indentation-based code for the inner block; whatever you do, when you `cat` the file the bash invocation must be visibly fenced.)

- [ ] **Step 3: Verify**

Run: `grep -nE '^## ' docs/re/recovery-normal-boot-fix-paths.md`

Expected: "Intended Fix Paths (Phase 2)" appears BEFORE "Recommended Mitigations (Ranked by Likelihood)".

---

## Task 4: Annotate the superseded mitigation

**Files:** Modify: `docs/re/recovery-normal-boot-fix-paths.md` (in-place note under section (a)).

- [ ] **Step 1: Locate section (a)**

The existing doc has:

```markdown
### (a) **AVB Partition-Read Facade (MOST LIKELY TO WORK)**
```

- [ ] **Step 2: Insert a "Why not pursued in shim" blockquote**

Immediately after the section-(a) heading line, before the `**Approach:**` paragraph, insert:

```markdown
> **Why not pursued in shim (2026-05-12):** the read path lives in userspace libfs_avb, outside our chainload domain. Effectively superseded by the disk-side graft (Phase 2); the facade idea remains noted here as a design alternative considered.

```

(Blockquote + trailing blank line so `**Approach:**` stays separated.)

- [ ] **Step 3: Verify**

Run: `grep -n 'Why not pursued in shim' docs/re/recovery-normal-boot-fix-paths.md`

Expected: exactly one hit between `### (a)` and `**Approach:**`.

---

## Task 5: Cross-link from README

**Files:** Modify: `README.md` (one sentence added to the Status section).

- [ ] **Step 1: Locate the Status section**

`README.md` lines 5–9:

```markdown
## Status

v2 architecture in flight. See `docs/superpowers/specs/` for the design and `docs/superpowers/plans/` for the implementation plan series.

Working artifacts: `dist/mode-0.efi` (pass-through observation build) and `dist/mode-1.efi` (protocol-hook fakelock via `QCOM_VERIFIEDBOOT_PROTOCOL` mutation; KM/Oplus see locked/green when stock images verify cleanly).
```

- [ ] **Step 2: Append a recovery sentence after "Working artifacts"**

Insert as a new paragraph after the "Working artifacts" line:

```markdown
Mode-1 supports the "stock recovery + custom system" use case by default. Custom recovery + normal boot requires a disk-side graft of stock vbmeta — see [`docs/re/recovery-normal-boot-fix-paths.md`](docs/re/recovery-normal-boot-fix-paths.md). Both a host script and a device-side companion module are Phase-2 work; neither ships today.
```

- [ ] **Step 3: Verify**

Run: `grep -n 'recovery-normal-boot-fix-paths' README.md`

Expected: exactly one hit.

---

## Task 6: Cross-link from avb-input-facade

**Files:** Modify: `docs/re/avb-input-facade.md` (prepend a banner above the existing H1).

- [ ] **Step 1: Read top of file**

Run: `head -3 docs/re/avb-input-facade.md`

Expected first line: `# AVB input façade plan for recovery/dtbo embedded vbmeta`.

- [ ] **Step 2: Insert banner before line 1**

Prepend:

```markdown
> **Status (2026-05-12):** The partition-read façade idea in this doc did **not** graduate into shim code. The path that did graduate (in spirit; code still Phase-2) is the disk-side graft documented in [`recovery-normal-boot-fix-paths.md`](recovery-normal-boot-fix-paths.md). This doc is preserved as a design alternative considered.

```

(Blockquote + blank line so the H1 stays on its own line.)

- [ ] **Step 3: Verify**

Run: `grep -n 'recovery-normal-boot-fix-paths' docs/re/avb-input-facade.md`

Expected: exactly one hit, above the existing H1.

---

## Task 7: Add "Historical — resolved" callout above Action Items

**Files:** Modify: `docs/re/recovery-normal-boot-fix-paths.md` (insert callout above existing Action Items section).

- [ ] **Step 1: Locate the Action Items heading**

The existing doc has a section starting with:

```markdown
## Action Items for gbl-chainload
```

The items below it read as live open hypotheses ("Confirm Hypothesis", "Proof of Concept", "Medium-term Production") — pre-resolution content that contradicts the new status banner. The callout warns the reader.

- [ ] **Step 2: Insert callout immediately above that heading**

Prepend (before the `## Action Items …` line, AFTER any preceding `---` horizontal rule):

```markdown
> **Historical — resolved 2026-05-12.** The action items below were written before the disk-side graft was validated. They are preserved for context. Live next work is tracked under Cleanup Phase 2 (separate spec), not against this doc.

```

(Blockquote + trailing blank line so the H2 stays on its own line.)

- [ ] **Step 3: Verify**

Run: `grep -n 'Historical — resolved 2026-05-12' docs/re/recovery-normal-boot-fix-paths.md`

Expected: exactly one hit, immediately above the `## Action Items for gbl-chainload` line.

Run: `grep -nE '^## ' docs/re/recovery-normal-boot-fix-paths.md`

Expected order of `##` headings (top to bottom): Executive Summary → (intermediate sections) → Intended Fix Paths (Phase 2) → Recommended Mitigations (Ranked by Likelihood) → (subsections under it) → Root Cause Summary → Action Items for gbl-chainload → References. Confirm "Intended Fix Paths" precedes "Recommended Mitigations" and "Historical — resolved" precedes "Action Items".

---

## Task 8: Verify acceptance

**Files:** none (verification only).

- [ ] **Step 1: No dangling references to the old filename**

Run: `rg -l 'aosp-early-avb-bootflow' .`

Expected: NO output. If anything matches, the cleanup phase 1 spec doc you're working alongside may still mention it — open each hit and replace with `recovery-normal-boot-fix-paths`. Re-run until clean.

- [ ] **Step 2: New cross-links present**

Run: `grep -n 'recovery-normal-boot-fix-paths' README.md docs/re/avb-input-facade.md`

Expected: exactly two hits, one per file.

- [ ] **Step 3: Original investigation content intact**

Run: `grep -c '^## Executive Summary\|^## Early-AVB Call Chain\|^## Cmdline Digest Handling\|^## Recovery Partition Hash Verification Failure Mode\|^## Hypothesis Validation\|^## Recommended Mitigations\|^## Root Cause Summary\|^## Action Items\|^## References' docs/re/recovery-normal-boot-fix-paths.md`

Expected: count ≥ 7. If lower, you over-deleted; the original analysis content must survive below the new front matter. Compare against `git log <old branch> -- docs/re/aosp-early-avb-bootflow.md` from a previous session if you need to recover.

---

## Task 9: Stage and commit

**Files:** all three modified files staged in a single commit.

- [ ] **Step 1: Review diff**

Run:
```bash
git status -s
git diff -- README.md docs/re/avb-input-facade.md
```

Expected:
- `?? docs/re/recovery-normal-boot-fix-paths.md` (the renamed file, currently untracked)
- ` M README.md`
- ` M docs/re/avb-input-facade.md`

`docs/re/aosp-early-avb-bootflow.md` is no longer present.

- [ ] **Step 2: Stage**

Run:
```bash
git add docs/re/recovery-normal-boot-fix-paths.md README.md docs/re/avb-input-facade.md
git status -s
```

Expected all three with `A ` or `M ` in the index column.

- [ ] **Step 3: Commit**

Run:
```bash
git commit -m "$(cat <<'EOF'
docs: recovery normal-boot fix-paths status doc (PR-a v2 of cleanup-phase-1)

Promotes the recovery investigation note to an authoritative status doc.

- Renames docs/re/aosp-early-avb-bootflow.md to
  docs/re/recovery-normal-boot-fix-paths.md.
- Adds a status banner naming the two intended fix paths, both
  Phase-2 work: a host-side scripts/graft-vbmeta-from-stock.py
  (TBD) and a device-side recovery-graft companion module (TBD).
- Adds an "Intended Fix Paths (Phase 2)" section above the preserved
  "Recommended Mitigations" analysis.
- Tags option (a) (AVB Partition-Read Facade) as superseded by the
  disk-side graft.
- Adds a "Historical — resolved 2026-05-12" callout above the
  legacy "Action Items" section so readers don't mistake those
  pre-resolution items for live work.
- Cross-links from README.md and docs/re/avb-input-facade.md.

Refs: docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md
(v2 spec, post-revert of PR #8 via PR #13).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Verify commit**

Run: `git log --oneline -1 && git show --stat HEAD`

Expected: one new commit on `feature/cleanup-p1a-recovery-fix-paths-doc-v2`. The `--stat` shows the renamed doc (git may collapse as rename or show A+D), `README.md`, and `docs/re/avb-input-facade.md`.

---

## Task 10: Push and open PR

**Files:** none.

- [ ] **Step 1: Push**

Run:
```bash
git push -u origin feature/cleanup-p1a-recovery-fix-paths-doc-v2
```

- [ ] **Step 2: Open PR**

```bash
gh pr create --title "docs: recovery normal-boot fix-paths status doc (PR-a v2 of cleanup-phase-1)" --body "$(cat <<'EOF'
## Summary
- Renames `docs/re/aosp-early-avb-bootflow.md` → `docs/re/recovery-normal-boot-fix-paths.md`.
- Adds a status banner + "Intended Fix Paths (Phase 2)" section naming a host script and device module, both Phase-2 work.
- Tags the AVB Partition-Read Facade idea as superseded by the disk-side graft.
- Adds a "Historical — resolved 2026-05-12" callout above the legacy Action Items section.
- Cross-links from `README.md` and `docs/re/avb-input-facade.md`.

Refs: `docs/superpowers/specs/2026-05-12-cleanup-phase-1-design.md` (v2). Sibling PRs (to come): cleanup-p1b regression guard, cleanup-p1c log stream split.

## Test plan
- [ ] `grep -n 'recovery-normal-boot-fix-paths' README.md docs/re/avb-input-facade.md` returns one hit per file.
- [ ] `grep -n 'Historical — resolved 2026-05-12' docs/re/recovery-normal-boot-fix-paths.md` returns one hit, above the `## Action Items` heading.
- [ ] `grep -c '^## Executive Summary\|^## Early-AVB Call Chain\|^## Recommended Mitigations\|^## Action Items\|^## References' docs/re/recovery-normal-boot-fix-paths.md` returns ≥ 5 (original analysis preserved).
- [ ] Manual read: new doc reads cleanly; "Intended Fix Paths (Phase 2)" appears above "Recommended Mitigations"; banner says both paths are Phase 2 (no claim of an existing script).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 3: Confirm PR is open**

Run: `gh pr view`

Expected: base = `main`, head = `feature/cleanup-p1a-recovery-fix-paths-doc-v2`. Note the PR URL.
