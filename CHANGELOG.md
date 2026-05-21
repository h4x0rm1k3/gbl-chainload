# Changelog

## v2.2.2 — 2026-06-21

Highlights:

- All ZIPs: Reworked UI, user prompts, and changed up device directory.
- ZIP mode-1 installer: Auto graft recovery.
- Add host tool instructions, and vbmeta graft python wrapper.

Fixes:

- Fastboot menu warns user correctly about ungrafted partitions now.

## v2.2.1 — 2026-05-20

Highlights:

- Diagnostic mode reworked: mode is now read from the GBLP1 overlay (a `MODE2_PROFILE` entry means mode-2) instead of a per-build base-EFI SHA-256 list, so the `unknown-base` label is gone and diag no longer needs a vendored-tool rebuild when an EFI changes.
- diag: removed the `confidence` headline; the `EFISP` / `loader-ABL` / `avb chain` lines (plus the in-bundle raw checks) stand on their own.
- diag: the action line is now a descriptive `avb chain` — `ok` for mode-2/clean, else `<parts> fail verified-boot — could require graft (mode-1 only)`. Stock chained sub-vbmeta (`vbmeta_system`, `vbmeta_vendor`) are excluded since they are never grafted.

Fixes:

- mode-2 installer `detect_oem` is recovery-safe: prefers `getprop`, falls back to `/prop.default`, `/default.prop`, then mounted `build.prop` (recovery has no mounted `system`/`vendor`).

## v2.2.0 — 2026-05-20

Highlights:

- Single-source `VERSION` file drives every consumer (`.dsc`, host tool Makefiles, installer `ui_print`, fastboot menu row, `gbl-chainload_version` getvar, `efisp-package.py --version`, on-screen banner via EDK II `-D` build macro).
- Linux x86_64 host-tool builds (zig musl-static) added alongside Windows/macOS.
- Diagnostic mode shipped: pre-reboot EFISP install confidence + `/sdcard/` bundle, new `gblp1-inspect` tool, `vbmeta-graft list-hash` subcommand.
- Universal TZ rollback-bump drop.
- AVB parser consolidation onto AvbParseLib.
- Release workflow: tag (`v*`) and dispatch triggered, draft GitHub Release with curated + auto-generated notes.

Upgrade notes:

- The hardcoded `GBL_CHAINLOAD_VERSION = 2.0` in `.dsc` is gone — builds now require the `VERSION` file at repo root.
- Host tools accept `--version`; `gbl-pack` no longer self-identifies as `1.0.0`.
- `--tools-dir` flag on `efisp-package.py` is preserved as an alias of the new `--bin-dir`.

## v2.1.0

Mode-2 ZIP implementation, TOML profile migration, EDK II escape fix.

## v2.0.0

Initial 2.x foundation: mode-0/1/2 build pipeline, GBLP1 overlay format, ABL patching toolchain.
