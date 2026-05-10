# Mode-1 ↔ Fakelocked-Stock Comparison — Track 2 Design

**Status:** approved — ready for implementation plan.

**Depends on:** Track 0 (`2026-05-10-test-device-hardening-design.md`)
and Track 1 (`2026-05-10-logfs-verbosity-audit-design.md`) ideally
merged first so any fresh re-captures benefit from richer logs. Not
strictly required since the comparison primarily reads existing logs.

## Goal

Verify that under mode-1 (patch9 active, grafted vbmeta footers on
custom recovery), the device builds the same Root of Trust and vbmeta
hash that the pure-stock fakelocked baseline produced. If they diverge,
patch9 is masking a real verifiedboot integrity break and we need to
know.

## Inputs (already on disk)

- **Mode-1 (current)**: any of `logs/20260510-150*_manual_manual_v2.0-plan1/`.
  Pick the most recent (`logs/20260510-150754_manual_manual_v2.0-plan1/`
  at spec time).
- **Fakelocked + stock baseline**: `~/gbl-chainload-dirty/logs/20260508-202149_manual_mode-fakelocked-stock-images_vf91f6f3/`
  — pure stock images flashed, fakelocked mode active, build `f91f6f3`
  (pre-patch9).
- **Stock-fastboot getvar baseline** (locked, untouched device, build
  `f91f6f3`): `~/gbl-chainload-dirty/logs/20260508-020314_stock_fastboot_getvar_all/`

## Approach

### Sub-task 1 — inventory + property table

List which artefacts each capture has (`bootloader_log`, `bootconfig`,
`cmdline`, `getprop.boot.txt`, `dmesg.txt`, `logfs/*`).

Build a side-by-side comparison table for these keys (from
`getprop.boot.txt` + `bootconfig` + `cmdline`):

- `ro.boot.verifiedbootstate`
- `ro.boot.vbmeta.device_state`
- `ro.boot.vbmeta.digest`
- `ro.boot.vbmeta.size`
- `ro.boot.vbmeta.hash_alg`
- `ro.boot.vbmeta.avb_version`
- `ro.boot.flash.locked`
- `ro.boot.veritymode`
- `androidboot.verifiedbootstate` (cmdline)
- `androidboot.vbmeta.public_key_digest`
- any `ro.boot.qcom.*` differing
- `androidboot.boot_devices`, `androidboot.slot_suffix`

### Sub-task 2 — bootloader_log diff

ABL emits ROT and verified-boot computation traces. Compare:

- "Root of Trust" / "RoT" mentions
- vbmeta hash computation lines
- "VerifiedBootInfo" lines (where our hook now intercepts)
- "DeviceUnlock" / "device locked" mentions
- SCM call traces around RoT submission

Annotate excerpts where the two diverge.

### Sub-task 3 — findings doc

Write `docs/re/mode-1-vs-fakelocked-stock-findings.md` with:

- The property table from sub-task 1
- Annotated bootloader_log excerpts from sub-task 2
- Interpretation: which divergences are *expected* (e.g. a different
  vbmeta digest because we have grafted footers AND patch9 active
  changes verification path) vs *anomalous* (e.g. ROT bytes that
  shouldn't depend on our patches differing)
- A concrete TL;DR verdict on "is patch9 doing the right thing?":
  pass / fail / inconclusive-need-more-data

## Validation

- The findings doc names a verdict in its first paragraph.
- If verdict is "anomalous", the doc lists which patch (probably patch9
  specifically) is the suspect, and what additional capture would
  confirm.

## Contingency

If the existing fakelocked baseline turns out to be insufficient
(missing artefacts, sparse bootloader_log, no relevant ROT lines), the
operator can re-capture: flash pure stock recovery + dtbo (no graft, no
chainload), boot with the older mode-fakelocked .efi (without patch9),
manually capture via `test-device-manual.sh`. The findings doc should
note which captures it ended up using.

## Out of scope

- Fixing whatever divergence is found (separate plan)
- Comparing to the no-ebs-debug-stock baseline (`~/gbl-chainload-dirty/logs/20260508-204459_manual_fakelocked-debug-no-ebs-stock-images_vcc8a48e/`)
  as a primary input — mention it as a "see also" if useful
- Implementing test automation for this comparison (one-off RE work)

## Files in scope

- `docs/re/mode-1-vs-fakelocked-stock-findings.md` (new)

No source code changes.

## Risks and known gotchas

- The fakelocked baseline was captured with build `f91f6f3` (pre-patch9).
  The mode-1 capture is post-Plan-3 with patch9 active. Some divergence
  is *intended* (patch9 changes vbmeta validation flow). The findings
  doc must distinguish "expected divergence" from "anomalous
  divergence" — that's the entire interpretation work.
- If the older capture's bootloader_log is sparser than current
  (different verbosity), interpretation gets harder. Track 1 (logfs
  verbosity) helps future captures but doesn't help re-interpreting
  older logs.
