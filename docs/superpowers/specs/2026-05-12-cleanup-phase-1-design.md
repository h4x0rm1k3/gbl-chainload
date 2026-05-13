# Cleanup Phase 1 — Lock In Findings, Shed Dead Surface

**Date:** 2026-05-12 (revised v2 after PR-#8 revert)
**Status:** Draft for review
**Scope:** Three independent sub-PRs that close the recovery-fix exploration cycle and prepare the tree for the Phase-2 on-device module suite (separate spec).

---

## Why this exists

The recovery-failure investigation (`docs/re/aosp-early-avb-bootflow.md`, 2026-05-10) reached a root cause: when mode-1 successfully fakes vbmeta for ABL, **AOSP `first_stage_init` re-walks the vbmeta descriptor tree from disk** and fails on the custom-recovery HASH mismatch. This is not a bootloader bug we can patch from our shim; it is an AOSP-init constraint that has to be fixed by re-shaping the on-disk recovery image *before* first-stage init reads it.

The fix has two intended forms — **both are Phase-2 work, neither lives on `main` today**:

- **Host-side script** — a `scripts/graft-vbmeta-from-stock.py` that pastes stock recovery vbmeta at `round_up(custom_image_size, 4 KiB)` against a custom recovery image. Validated on infiniti during the abandoned `feature/synthesize-fastboot-cmd` exploration (see `memory/graft_at_natural_offset_wins.md`). The script itself was not landed; the technique is captured in memory and will be rebuilt under Phase 2.
- **Device-side companion module** — performs the same graft against the on-device stock vbmeta automatically, post-recovery-flash. Lives next to the OTA-cached-ABL module in the Phase-2 module suite.

While converging on this, we built three *exploratory* paths inside the bootloader: `oem synthesize-and-flash`, `oem graft-from-staged`, and `oem fix-vbmeta-footer`. **None of these are on `main`** — they live only on an edk2 submodule branch (`fastboot/synthesize-and-flash`) and a now-abandoned main-repo feature branch. PR #8 (`feature/synthesize-vbmeta-host`) landed a host helper `scripts/synthesize-vbmeta.py` that fed the on-device synthesize path; PR #13 reverted PR #8 because the on-device consumer never landed, leaving the host tool stranded.

So as of `2026-05-12 / main HEAD = 061a20d`, the synth/graft trajectory is **fully off main**. This phase locks in what we learned, gates the surface from being re-introduced accidentally, and untangles the log stream — before the Phase-2 module spec begins.

---

## Sub-PRs

This spec covers three sub-PRs, each landing as its own feature branch against `main` per the project's PR-only workflow (`CLAUDE.md`).

### PR-a: Recovery-failure decision doc (P1)

**Goal:** Promote the recovery investigation to an authoritative status doc that names the two intended fix paths (both Phase 2) and explicitly records why the bootloader shim does not carry the fix.

**Changes:**

1. **Rename** `docs/re/aosp-early-avb-bootflow.md` → `docs/re/recovery-normal-boot-fix-paths.md`. (The file is currently untracked in the working tree — it was authored locally but never committed; PR-a is the first commit to land it.)

2. **Add a status banner** at the top:
   > **Status (2026-05-12):** Confirmed. Custom-recovery + normal-boot under mode-1 fails in AOSP `first_stage_init` (libfs_avb), which re-walks the vbmeta descriptor tree from disk after our shim has already lied to ABL's KM. The fix has to re-shape the on-disk recovery image *before* first-stage init reads it. Two intended paths, **both Phase-2 work**: a host-side `scripts/graft-vbmeta-from-stock.py` and a device-side recovery-graft companion module. The bootloader shim does not carry the fix because the read path it would need to intercept lives in userspace libfs_avb, outside our chainload domain. The graft technique was validated on infiniti during exploration; technique is preserved in `memory/graft_at_natural_offset_wins.md` until Phase 2 rebuilds the tooling.

3. **Prepend a new "Intended Fix Paths (Phase 2)" section** above the existing "Recommended Mitigations" analysis:

   > #### Host path
   > A `scripts/graft-vbmeta-from-stock.py` that takes `--partition recovery --in <custom>.img --stock <stock-recovery>.img --out <patched>.img` and writes stock recovery vbmeta at `round_up(custom_image_size, 4 KiB)`. With `patch10` in the boot path, ABL emits `verify_result_local=OK` for recovery; the slot-level recoverable error is caught by `patch10` → final OK. Script TBD under Cleanup Phase 2.
   >
   > #### Device path
   > On-device companion module that performs the same graft against the on-device stock vbmeta, automatically, post-recovery-flash. Lives next to the OTA-cached-ABL module. Spec TBD.

4. **Cross-link from `README.md`** in the Status section: one sentence noting that mode-1 covers "stock recovery + custom system" by default; custom recovery + normal boot requires a future Phase-2 graft (host script or device module), not yet shipping.

5. **Cross-link from `docs/re/avb-input-facade.md`** at the top so anyone landing on the facade doc knows the partition-read facade idea did not graduate into shim code; the path that did graduate (in spirit, not in code yet) is the disk-side graft (host script + Phase-2 module).

6. **Update the existing "Recommended Mitigations" section** in place: under "(a) AVB Partition-Read Facade", add a one-line subsection — *"Not pursued in shim because the read path lives in userspace libfs_avb. Effectively superseded by the disk-side graft (Phase 2); the facade idea remains noted here as a design alternative considered."*

7. **Add a "Historical — resolved 2026-05-12" callout** above the existing "Action Items for gbl-chainload" section in the renamed doc. The original action items read as live open hypotheses ("Confirm Hypothesis", "Proof of Concept", "Medium-term Production"); the callout warns the reader those items are pre-resolution context, not live work. Wording:
   > **Historical — resolved 2026-05-12.** The action items below were written before the disk-side graft was validated. They are preserved for context. Live next work is tracked under Cleanup Phase 2 (separate spec), not against this doc.

**Out of scope for PR-a:**
- Any code change.
- README architecture rework (deferred to Phase 4).
- Re-introducing the graft host script (deferred to Phase 2).

**Acceptance:**
- `git log --oneline docs/re/recovery-normal-boot-fix-paths.md` shows a single commit (the rename + edits land together).
- `README.md` links to the new path.
- `rg 'aosp-early-avb-bootflow' .` returns no hits.
- The new file contains both the original investigation content (Executive Summary, Early-AVB Call Chain, …) AND the new front-matter sections (banner + Intended Fix Paths + Historical callout).

---

### PR-b: Regression guard for the synth/graft surface (P2)

**Goal:** PR #13 already removed the only on-main remnant of the synth/graft trajectory (`scripts/synthesize-vbmeta.py`). PR-b adds a forward-guard test + historical banners on the memory notes so the surface stays dead and the lessons stay readable.

**Why a separate PR for this:** the test is independently useful (it catches accidental re-introduction in any future PR), and the memory-note banners ground future readers in why those notes describe artifacts that no longer exist.

**Changes — main repo:**

1. **Create `tests/050_no_synth_graft_surface.sh`** — host-side regression check. Greps the main repo (excluding `docs/`, `memory/`, `.re-notes/`, and the test file itself) for the three command literals `synthesize-and-flash`, `graft-from-staged`, `fix-vbmeta-footer`. Exits non-zero if any appears in code or scripts. Add an entry for it in `tests/runall.sh`.

2. **Add a historical banner to `memory/graft_at_natural_offset_wins.md`** (one-line blockquote at top, after any frontmatter):
   > **Historical (2026-05-12):** Technique confirmed on infiniti during the abandoned `feature/synthesize-fastboot-cmd` exploration. Surface fully reverted from `main` by PR #13. Captured here for whoever rebuilds the host script + on-device module under Cleanup Phase 2.

3. **Add a historical banner to `memory/avb_constructed_verify_blocked.md`**:
   > **Historical (2026-05-12):** Resolved in spirit by the disk-side graft path (see `graft_at_natural_offset_wins.md`); kept here for context on why constructed verify was abandoned.

**Out of scope for PR-b:**
- Any edk2 work. The submodule branch `fastboot/synthesize-and-flash` carries the on-device experiments but is *not pulled into main's submodule pointer*. Phase 4 will dispose of the orphan branch on the edk2 remote.
- Any change to `DynamicPatchLib`, patches 1–10, or the mode-1 protocol hooks.
- Touching `tools/abl-patcher` or `tools/fv-unwrap`. Both stay; Phase 2 turns them Android-runnable.

**Acceptance:**
- `tests/050_no_synth_graft_surface.sh` exits 0 against post-PR-b `main`.
- `tests/runall.sh` includes the new test entry and the suite runs to completion.
- `rg 'Historical \(2026-05-12\)' memory/` returns exactly two hits.

---

### PR-c: Logging stream split (P3)

**Goal:** Stop flooding `UefiLogN.txt` with our verbose output. `UefiLogN.txt` is owned by QCOM BDS and carries the PBL → XBL → ABL → BDS prelude that is otherwise lost; our DEBUG/INFO spam currently truncates that prelude. Emit our stream into a dedicated file.

**Background:** Memory note `active_investigation_log_flush.md` documents the two-stream design and the `LogFsWrite` auto-flush behavior. The two-stream files already exist (`GblChainload_BootN.txt`); this PR (a) renames the file constant to `gbl-chainload_BootN.txt` for casing-convention consistency, and (b) flips the routing so verbose lines no longer mirror to UefiLog.

**Changes — main repo:**

1. **`GblChainloadPkg/Library/LogFsLib/DebugSink.c`** — gate UefiLog writes on `EFI_D_ERROR`. Every DEBUG line still lands in the gbl-chainload handle; only ERROR-tagged lines also land on UefiLog.

2. **Boundary markers** ("gbl-chainload entered" / "gbl-chainload exiting (rc=…)") emitted at `EFI_D_ERROR` so they survive the gate and remain visible on UefiLog. Existing markers get promoted; if missing, added.

3. **Rename the gbl-chainload filename constant** from `GblChainload_Boot` to `gbl-chainload_Boot` (wherever defined inside `LogFsLib/`).

4. **Flush contract** (per `memory/active_investigation_log_flush.md` and `canoe_simplefs_flush_contract.md`) is preserved verbatim — `LogFsWrite` keeps calling `Flush` after every successful write to the gbl-chainload handle. This is non-negotiable.

5. **No `scripts/build.sh` flag changes.** `--verbose` keeps verbose; output just lands in a different file.

6. **Create `tests/051_log_stream_split.sh`** — host-side regression check on the filename constant (verifies new lowercase form present, old form absent).

**Out of scope for PR-c:**
- Changing log levels (verbosity audit is a separate spec, `2026-05-10-logfs-verbosity-audit-design.md`, which can now proceed against the new file).
- Touching the canoe SimpleFS flush mechanism.
- Renaming pre-existing UefiLog files retroactively.

**Acceptance:**
- After a boot run with `--debug --verbose`, `logs/<run>/logfs/` contains both `UefiLogN.txt` and `gbl-chainload_BootN.txt`.
- `UefiLogN.txt` shows the QCOM BDS prefix intact (PBL → XBL → ABL framing visible at the top), followed only by our two boundary markers + any ERROR-class lines.
- `gbl-chainload_BootN.txt` carries the patch-engine and protocol-hook verbose output.
- `tests/051_log_stream_split.sh` exits 0.

---

## Cross-cutting

### Order of landing

**PR-a → PR-b → PR-c.** PR-a is doc-only, lands in minutes. PR-b is two banner edits + one new shell test. PR-c is the only one with C code changes; it lands last so a regression there can't cascade into the smaller doc PRs.

### Branch hygiene

Each PR is its own feature branch off `main`:
- `feature/cleanup-p1a-recovery-fix-paths-doc-v2`
- `feature/cleanup-p1b-regression-guard`
- `feature/cleanup-p1c-log-stream-split`

(The `-v2` suffix on PR-a distinguishes it from the closed v1 branch on origin. PR-b's name reflects its slimmed scope. PR-c is unchanged from v1's plan.)

### Safety

All three PRs respect `CLAUDE.md`:
- No fastboot flash commands.
- No `oem (un)lock`, no `flashing (un)lock_*`.
- Verification path stays `fastboot stage dist/<artifact>.efi` + `fastboot oem boot-efi`.
- PR-c can be smoke-tested against the test device with that two-command pair. PR-a and PR-b are doc/host-side only and need no on-device test.

### What this phase deliberately does NOT do — forward-look at Phases 2–4

**Phase 2 — On-device module suite (separate spec).** Two distinct modules, riding together but conceptually independent:

- **OTA-cached-ABL module.** On post-OTA boot, reads the new ABL from the OTA slot (`/dev/block/by-name/abl_<inactive>`), runs `abl-patcher` against it, and caches the patched output in EFISP for the next chainload. If patches fail, the module fails loud; the user falls back to a manually-stashed backup ABL (e.g., `/data/local/tmp/backup_abl` — module documents how/when the user creates this). Required for the eventual A16→A17 class of OTAs where an ABL may ship with the GBL/EFISP loader path removed; the cached patched ABL is the only way gbl-chainload keeps running across that kind of upgrade.
- **Recovery-graft module.** Performs the disk-side graft (the technique in `memory/graft_at_natural_offset_wins.md`) on-device, against the on-device stock vbmeta. Triggered when the user flashes a custom recovery. This is PR-a's device-side path.

Tools (already in tree, no new ones): `tools/abl-patcher` (becomes Android-runnable via NDK/PIE static — Phase-2 work, not Phase-1), `tools/fv-unwrap` (same). The host-side graft script (`scripts/graft-vbmeta-from-stock.py`) is rebuilt fresh under Phase 2, not salvaged from the abandoned `feature/synthesize-fastboot-cmd` branch.

**EFISP capacity** is not a constraint at Phase-2 scope. Confirmed `wc -c </dev/block/by-name/efisp = 3,145,728` (exactly 3 MiB) on infiniti. Current `dist/gbl-chainload.efi` is ~564 KiB; stock infiniti ABL is 272 KiB; mode-1 cache (gbl-chainload + cached ABL) totals ~836 KiB, leaving ~2.2 MiB free. Diff/patch encoding for the cached ABL is **deferred** as future optimization, gated on first contradicting size data from another device variant.

**Phase 3 — Mode-2 profiles (separate spec).** Mode-2's TZ-spoof mechanism is already done; what it needs is **per-OTA profiles** — a human-readable (yaml or toml) description of the RoT data mode-2 should assert. Profiles are produced by a host-side build tool and consumed by a new on-device **profile-manager module** that caches them in EFISP next to the cached ABL. Profile lifecycle mirrors vendor blobs + security patch level: when LineageOS (or any Custom ROM) bumps vendor, the profile bumps. Phase 3 also formally **drops mode-3** from `README.md`, `scripts/build.sh`, and any code paths — never implemented, no users. Modes 0, 1, 2 stay.

**Phase 4 — Tree and history cleanup (separate spec).** Final pass after Phases 2 and 3 stabilize:

- Move `docs/` out of the main repo (separate repo, or `.gitignored` notes locally). Our docs lean on personal-rule ambiguity that doesn't belong in version history.
- Rebase `main` to squash exploratory commits.
- Collapse `README.md` to a single short file that reflects the post-Phase-2/3 state.
- Force-push tidy or delete on the orphan `edk2` submodule branch `fastboot/synthesize-and-flash`.
- Delete the abandoned-and-now-merged remote branches kept for safety during this phase: `feature/synthesize-fastboot-cmd` (still on origin, not merged), the closed v1 cleanup-phase branches (`feature/cleanup-phase-1-spec`, `feature/cleanup-p1a-recovery-fix-paths-doc`), and the merged `feature/revert-synthesize-vbmeta-host`.
- Review remaining `FastbootMenu` entries and trim anything not useful after Phase 2 lands.

**Anti-scope for this phase (Phase 1):**

- Does not start any of Phase 2's modules.
- Does not touch mode-2, mode-3, or the build matrix.
- Does not move docs, rebase main, or rewrite README beyond a single Status-section sentence in PR-a.
- Does not force-push or delete the edk2 submodule branch.
- Does not re-introduce any synth/graft host or device code.

---

## Risks

1. **Log file index race (PR-c).** If `UefiLog{N}.txt` and `gbl-chainload_Boot{N}.txt` derive `N` independently they can desynchronize after a crash. Mitigation: derive `N` once at init from existing `Rotation.c` logic; both handles share the same index.
2. **Memory notes drift (PR-b).** Marking notes "historical" without deleting them risks future-me trusting stale advice. Mitigation: every historical banner names this spec's date and explicitly points at the surviving source of truth (`memory/graft_at_natural_offset_wins.md` for the technique, this spec for the trajectory).
3. **Reader confusion on the renamed doc (PR-a).** The original "Action Items for gbl-chainload" section reads as live tasks. Mitigation: the "Historical — resolved 2026-05-12" callout above it warns the reader explicitly; the status banner at the top sets the same expectation.

---

## Verification plan

After all three PRs merge:

1. `./scripts/build.sh --mode 0` and `--mode 1` build clean.
2. `fastboot stage dist/mode-1.efi && fastboot oem boot-efi` boots the test device into Android with KM 0x208 green (same as pre-phase).
3. `logs/<latest>/logfs/UefiLog0.txt` shows the QCOM BDS prefix; `gbl-chainload_Boot0.txt` exists and carries our verbose output; `UefiLog0.txt` contains the two boundary markers (entered + exiting).
4. `tests/050_no_synth_graft_surface.sh` and `tests/051_log_stream_split.sh` both exit 0 from `tests/runall.sh`.
5. `rg -l 'synthesize-and-flash|graft-from-staged|fix-vbmeta-footer'` returns only files under `docs/`, `memory/`, `.re-notes/`, or the regression test itself.
6. `README.md` Status section links to `docs/re/recovery-normal-boot-fix-paths.md` and names the host + device fix paths as Phase 2.

---

## References

- `docs/re/aosp-early-avb-bootflow.md` (renamed by PR-a to `recovery-normal-boot-fix-paths.md`) — recovery failure root cause + intended fix paths.
- `memory/graft_at_natural_offset_wins.md` — graft technique confirmation 2026-05-12. The technique source of truth.
- `memory/active_investigation_log_flush.md` — two-stream logging design rationale + flush contract.
- `memory/canoe_simplefs_flush_contract.md` — flush semantics PR-c must honor.
- `memory/avb_constructed_verify_blocked.md` — context on why constructed verify was abandoned in favor of graft.
- `CLAUDE.md` — safety and PR-only workflow rules that bound this phase.
- `tests/images/op15-infiniti-201-abl.img` (278,528 B) — stock ABL size reference for EFISP capacity analysis.
- EFISP partition size on infiniti: `wc -c </dev/block/by-name/efisp = 3,145,728` (3 MiB exactly) — confirmed 2026-05-12.
- PR #13 — the revert that cleaned `scripts/synthesize-vbmeta.py` and `tests/053_synthesize_vbmeta_roundtrip.sh` off main. Merged at commit `061a20d`.
- Abandoned (still on origin, kept until Phase 4): `feature/synthesize-fastboot-cmd` (5 unmerged commits), edk2 `fastboot/synthesize-and-flash` branch (6 experimental commits, no submodule pointer).
