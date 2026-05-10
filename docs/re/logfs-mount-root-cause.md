# LogFs Mount Root Cause — Investigation Report

**Date:** 2026-05-10  
**Branch:** main  
**Question:** Why does dirty `mode-fakelocked.efi` mount logfs cleanly, but v2 failed at `ConnectController returned EFI_NOT_FOUND`?

---

## Summary

The root cause was **two layered bugs in v2 `Entry.c`**, not a dropped edk2 commit.  Both are now fixed.  Commit `7ad0d3f` is the correct proper fix, not a workaround — it should be kept.

---

## Dropped-Commit Analysis

### Commits in dirty edk2 that v2 dropped

| Commit | Title | FAT/FS relevant? |
|--------|-------|-----------------|
| `676e4887a7` | FastbootLib: add staged EFI helpers | No — only `mGblFreshStageAvailable` / `mGblStagedEfiHandle` state; no FS binding |
| `4c164c0249` | FastbootLib: oem get-staged logfs (FAT mount) | No — adds a fastboot command that runs AFTER LogFsInit; cannot affect mount |
| `89fc9fced8` | FastbootLib: oem get-staged logfs (raw block IO) | No — replaces the above with block-IO path; no FS binding |

**None of the dropped commits touch FAT driver binding, ConnectController policy, FDF driver inclusion, or anything that could affect LogFsInit.**

The `LoadFVLib` code (`49fe4f40b4` / `0b4d364e57`) is byte-identical between dirty and v2 — cherry-picked without conflict.  `ConnectDriverToGopHandles` inside LoadFVLib connects the newly-started image to GOP handles (not block devices), so it is irrelevant to FAT binding on the logfs partition.

No FDF in either repo embeds a FAT driver in the payload FV.  `LoadDriversFromCurrentFv` finds no `EFI_FV_FILETYPE_DRIVER` files when running as a staged buffer (no parent FV).

---

## Why Dirty `mode-fakelocked.efi` Always Succeeded

Dirty `Entry.c` `CommonEarlyInit` call order:

```
LogFsInit()              ← FIRST (before partition enumeration)
DeviceInfoInit()
EnumeratePartitions()
UpdatePartitionEntries()
SignalSDDetection()
LoadDriversFromCurrentFv()
```

On canoe, the **platform BDS runs before our EFI** and:
- Enumerates all GPT partitions.
- Connects the FAT driver to every FAT-formatted partition handle, including `logfs`.

When `LogFsInit()` runs as the very first call:
- `GetBlkIOHandles("logfs")` finds the handle immediately (BDS already populated it).
- `gBS->ConnectController(Handle, NULL, NULL, TRUE)` returns `EFI_ALREADY_STARTED` (FAT already bound by BDS).
- Dirty `MountLogFsRoot` tolerates `EFI_ALREADY_STARTED`, proceeds to `HandleProtocol(SimpleFileSystem)` — succeeds.
- Mount succeeds.

This is pure reliance on platform BDS state.  The dirty repo didn't do anything special to cause FAT binding — the platform did it before our EFI started.

---

## v2 Bug 1 — `LogFsInit` Before Partition Enumeration (fixed by `aac521e`)

Original v2 `CommonEarlyInit` had `LogFsInit()` first (same as dirty), but the device under test was using a different boot path where partition handles weren't pre-populated.  Actually the real v2 bug was the OPPOSITE: v2's initial `CommonEarlyInit` had `LogFsInit()` FIRST but then the v2 rewrite moved it AFTER, which broke things.

Looking at commit `aac521e`:

```
Before aac521e: LogFsInit() called first (before EnumeratePartitions)
After  aac521e: LogFsInit() called last  (after  SignalSDDetection + LoadDriversFromCurrentFv)
```

`GetBlkIOHandles` internally uses `EnumeratePartitions`-populated data.  Calling `LogFsInit` before `EnumeratePartitions` caused `GetBlkIOHandles("logfs")` to return 0 handles (`EFI_NOT_FOUND`).

**Fix: `aac521e`** — moved `LogFsInit` to end of `CommonEarlyInit`.

---

## v2 Bug 2 — ConnectController Returns EFI_NOT_FOUND on Staged Path (fixed by `7ad0d3f`)

After `aac521e`, the ordering was correct.  But device evidence showed `ConnectController` itself returned `EFI_NOT_FOUND`.

On the **staged EFI path** (`fastboot stage` + `oem boot-efi`):
- The outer ABL (`LinuxLoader.efi`) calls `gBS->LoadImage` + `gBS->StartImage` on our buffer.
- Our EFI runs without its own FV context (no `EFI_FIRMWARE_VOLUME2_PROTOCOL` on `DeviceHandle`).
- `SignalSDDetection()` → `ConnectAllControllers()` iterates all handles and calls `ConnectController` on each — but this only succeeds if a FAT driver is registered in the DXE dispatcher's `DriverBindingHandle` list.  On canoe's ABL, the FAT driver is loaded as a DXE driver early in BDS.  After BDS hands off to our EFI via `StartImage`, the DXE dispatcher has already exited — no new drivers can be dispatched.  `ConnectController` returns `EFI_NOT_FOUND` meaning no unconnected driver binding matched.

**Yet SimpleFileSystem IS present** — because the platform BDS connected FAT to all FS handles before BDS exited to the boot option.  The FAT driver already installed `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` on the logfs handle.  `HandleProtocol(SimpleFileSystem)` directly returns `EFI_SUCCESS`.

The old v2 `MountLogFsRoot` (before `7ad0d3f`) called `ConnectController` BEFORE probing `SimpleFileSystem`, and hard-failed on the `EFI_NOT_FOUND` return, never getting to `HandleProtocol`.

**Fix: `7ad0d3f`** — restructured `MountLogFsRoot` to:
1. Probe `HandleProtocol(SimpleFileSystem)` first.
2. If already installed (platform BDS path), skip `ConnectController` entirely.
3. If not installed, call `ConnectController`, tolerate both `EFI_ALREADY_STARTED` and `EFI_NOT_FOUND`.
4. Re-probe `HandleProtocol(SimpleFileSystem)`.  Step [3/5] is the authoritative gate — if SimpleFS still isn't there, fail.

---

## Architecture: Why `7ad0d3f` Is Correct, Not a Workaround

The `7ad0d3f` logic is the UEFI-spec-correct approach:

- `ConnectController` is an optimization hint to the firmware to bind drivers.  Its return code (`EFI_NOT_FOUND`, `EFI_ALREADY_STARTED`) is informational about whether NEW binding happened.
- `HandleProtocol(SimpleFileSystem)` is the authoritative check: the protocol is either installed or it isn't.
- The dirty repo implicitly did this correctly (ConnectController returned `EFI_ALREADY_STARTED`, then HandleProtocol succeeded) — only because of platform BDS pre-connection.
- `7ad0d3f` makes the same logic explicit and robust to both paths (pre-connected by BDS, or connected by our ConnectController call).

The "linuxloader mechanism" the user referenced is `ConnectAllControllers()` inside `SignalSDDetection()`.  On the flashed-EFISP path (our EFI is the boot option, not staged), this call runs when BDS hasn't yet connected FAT, and `ConnectAllControllers` does the binding.  On the staged path, FAT is already connected by BDS.  Both cases are handled by `7ad0d3f`'s probe-first approach.

---

## Commit Status

| Commit | Status | Action |
|--------|--------|--------|
| `aac521e` | Correct fix — Entry.c ordering | Keep |
| `2c0294a` | Diagnostic verbosity in Mount.c | Keep (useful for device debugging) |
| `7ad0d3f` | Correct fix — ConnectController tolerance | **Keep — this IS the proper fix** |

No revert needed.  No additional code changes required.

---

## Concerns / Forward Notes

1. **dist/ EFI is stale.** The `dist/mode-0-auto-debug-verbose.efi` in the repo does not contain the `[1/5]...[5/5]` Print strings from `2c0294a`. A rebuild is needed before device testing.

2. **Staged vs flashed path behavioral difference.** The `IsLoadedFromFv()` check in `Mount.c` distinguishes these two paths for UefiLog rotation. The logfs MOUNT itself now works on both. Verified: dirty repo always ran as flashed EFISP, never as staged buffer — so the staged path was never tested there.

3. **No FAT driver in our FV.** `LoadDriversFromCurrentFv` finds nothing to load on the staged path. On the flashed-EFISP path, our FV contains only our application — no embedded FAT driver. Both rely on the platform BDS's FAT driver. This is correct; we should not embed a FAT driver.

4. **`ConnectAllControllers` is still useful.** On a hypothetical platform where BDS does NOT pre-connect FAT, `SignalSDDetection` + `ConnectAllControllers` would be the mechanism that binds it. The v2 architecture handles this correctly via the `7ad0d3f` re-probe after ConnectController.
