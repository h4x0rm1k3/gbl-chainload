# Ranked Cost-Center Analysis for `gbl-chainload` on SM8550+ Qualcomm Devices

## TL;DR
- **The dominant pre-chainload cost on SM8550+ is almost certainly ABL unwrap + LZMA decompression (cost center 4), followed by LogFs mount/rotation (3) and the LoadImage/StartImage handoff (7).** Together these three plausibly consume 60–80% of `gbl-chainload`'s wall-clock budget. Partition enumeration (1), FV driver loading (2), dynamic patch scans (5), and protocol-hook installation (6) are real but second-order costs, each likely tens of ms or less on Cortex-X3-class hardware at boot-stage clocking.
- **The single best optimization is to stop reading and decompressing the full 8 MiB `abl_a` partition: parse the FV header, read only the LZMA-compressed FFS section, and decompress directly into the staging buffer.** Combined with eliminating the second `LogFsInit()` call and bounding patch scans to PE `.text`, this should reclaim the majority of avoidable time with low reliability risk.
- **Trustworthy numbers for the exact `gbl-chainload` wall-clock are not published**, so all figures below are order-of-magnitude estimates derived from EDK2 source behaviour, public LZMA/ARM benchmarks, Linaro/U-Boot Qualcomm boot logs, and the partition layouts of OnePlus 11/13 and Xiaomi 14 (SM8550/SM8650). Instrument first with `mrs x0, cntvct_el0` before doing anything irreversible.

## Key Findings

**Hardware envelope.** SM8550 (Cortex-X3 + A715/A710 + A510) and SM8650/SM8750 (X4 + A720/A520) flagships ship UFS 4.0 (KIOXIA's UFS 4.0/4.1 brief cites "theoretical data transfer speeds of up to 23.2 gigabits per second (Gbps) per lane, or 46.4 Gbps per device"; Micron claims ~4.3 GB/s sequential read). UFS 3.1 floor is ~2.1 GB/s. In bootloader stage, neither the CPU nor the UFS link is configured for peak throughput. Public Linaro/U-Boot logs show stock XBL reporting `Flash Throughput, 262000 KB/s` (≈262 MB/s) and `DDR Frequency, 1555 MHz` on RB5-class Snapdragon, far below DRAM/UFS peaks. The CPU is typically left clamped low by stock firmware — AOSP's official boot-time-optimization guide states verbatim: "with a fully modular kernel, the init ramdisk decompression takes place before the CPUfreq driver can be loaded. So, if the CPU is left at the lower end of its frequency by the bootloader, the ramdisk decompression time can take longer." Treat the active CPU as **~1–2 GHz, single-core, no SIMD assist** for pre-OS bootloader work unless someone in the chain has explicitly raised CPU frequency.

**Partition topology.** OnePlus 11 (salami / SM8550) places `abl_a` at `/dev/block/sde10` and `abl_b` at `/dev/block/sde42` — LUN `sde` alone exposes at least 42 partitions per slot region. Older OnePlus 6T device trees show `sde` running through `sde72` (ImageFv). The OnePlus 13 (PJZ110, SM8650) OTA payload lists ~50 distinct firmware partition names per slot. Counting A/B duplication plus userdata/super/persist on other LUNs, the **total exposed GPT/BlockIo handle count is realistically 100–180** on modern flagships — the ~160 figure in the handoff is accurate.

**ABL image format and size.** Qualcomm ABL since the Snapdragon 835 era is a UEFI Firmware Volume containing a single compressed FFS section. Public reverse-engineering by worthdoingbadly.com on the Pixel 2 XL (`taimen`, Snapdragon 835/MSM8998) states: "The XBL's format also changed: the inner Firmware Volume is now compressed with Gzip." However, the EDK2-side build (`QcomModulePkg` / `abl_tianocore_edk2`) and XDA modding documentation describe the section as **LZMA-encapsulated** (FV GUID `EE4E5898-3914-4259-9D6E-DC7BD79403CF`, the IntelFrameworkModulePkg LZMA decompress GUID) wrapping `LinuxLoader.efi`. XDA modders document splitting `abl` at offset `0x3078` and treating the tail as LZMA. The decompressed `LinuxLoader.efi` is small on older devices (376832 bytes on a Pixel-class ABL) but flagship ABLs grow with OEM features. On-disk **`abl_a` partition is 0x800000 = 8 MiB** on Qualcomm reference designs (confirmed by OnePlus Nord `fastboot getvar partition-size:ablbak: 0x800000`); actual compressed image payload is typically ~200 KB – 2 MB (OnePlus 8T flash log: `Sending 'abl' (1996 KB)`; Xiaomi 14 houji: `Sending 'abl_ab' (235 KB)`).

**Why ABL unwrap dominates.** LZMA decompression speed on ARM is the well-known bottleneck. The LZMA SDK README (Igor Pavlov, mirrored in EDK2 upstream) quotes the historical baseline verbatim: "Estimated decompressing speed: 20-30 MB/s on 2 GHz Core 2 or AMD Athlon 64 / 1-2 MB/s on 200 MHz ARM, MIPS, PowerPC or other simple RISC." Modern Cortex-X3/X4 at ~2 GHz are roughly 4–8× the historical Cortex-A53 reference; EDK2's `LzmaCustomDecompressLib` is the stock LZMA SDK without ARM64 asm assist (the README explicitly says "Out-of-Order execution capability is not so important for LZMA Decompression. Most of the code is 32-bit integer code"). A realistic decompression rate at boot clock is **~30–80 MB/s of output** on Cortex-X3-class cores at half-clock. Decompressing a ~1 MB compressed → ~3 MB uncompressed ABL therefore costs roughly **40–100 ms** of pure CPU; plus the **full-partition `ReadBlocks` of 8 MiB at ~262 MB/s ≈ ~30 ms** (worse if BlockIo is not connected at full UFS speed). Patched ABLs that have grown to several MiB compressed can push this well past 150 ms.

**LoadImage/StartImage handoff.** EDK2 `CoreLoadImage` on AArch64 walks PE/COFF relocations, allocates pages, copies sections, then calls `InvalidateInstructionCacheRange` over the full image range. For multi-MiB images this is dominated by the section copy (memory-bandwidth bound) plus the I-cache flush, which on Cortex-X3 with point-of-unification IC IVAU instructions runs per-cacheline (typically 64 B). A ~3 MiB image is ~49K cachelines — bounded by IC IVAU throughput, **expect 5–20 ms**. Note: the handoff is unavoidable, but ABL is already decompressed once; if `gbl-chainload` is feeding `LoadImage` a buffer that is *itself* still a wrapped FV, EDK2 will re-walk and re-decompress, doubling the cost.

**LogFs / FAT mount cost.** `ConnectController(handle, NULL, NULL, TRUE)` on the BlockIo handle for `userdata` (or whatever LogFs partition is used) triggers the PartitionDxe driver, which reads the GPT header + entries (typically 2 reads of ~17 KB), then for each partition installs BlockIo + DevicePath; FAT or SimpleFs binding then mounts the FS — reading BPB, FATs, and root directory. On UFS-class storage this is usually **~10–40 ms per mount**. The handoff explicitly notes `LogFsInit()` is called **twice** (CommonEarlyInit and again in BootFlowChainLoad after closing) — the second one likely re-traverses FAT directory state and may re-trigger `ConnectController`, costing **another ~10–40 ms**. Log rotation that copies filesystem data adds linearly with log size, and FAT writes are slow because each write may dirty the FAT and root-dir sectors. This is the most likely "hidden" cost center.

**Partition enumeration (`LocateHandleBuffer` + `UpdatePartitionEntries`).** EDK2's `LocateHandleBuffer(ByProtocol, ...)` walks the global handle database (doubly-linked list); with ~160 BlockIo handles each call is **microseconds, not milliseconds**. The Qualcomm-specific `UpdatePartitionEntries()` (in `QcomModulePkg/Library/BootLib/PartitionTableUpdate.c`, CodeLinaro `clo/la/abl/tianocore/edk2` `uefi.lnx.3.0.r1` branch) iterates all LUNs and re-reads/re-parses GPT — that *is* I/O. On modern multi-LUN UFS (typically 4–6 LUNs), that's perhaps 4–10 ms total. Repeated calls are wasteful but not catastrophic.

**FV driver loading.** `LoadDriversFromCurrentFv(ImageHandle)` enumerates FFS entries of driver type and calls `LoadImage` on each. Per the dsc — `AblUnwrapLib`, `DynamicPatchLib`, `ProtocolHookLib`, `LogFsLib`, `LzmaCustomDecompressLib`, `LoadFVLib` — most of these are libraries linked into the application, not separately dispatched DXE drivers. **Expected cost: <5 ms unless the FV embeds unexpected drivers.** Not a top concern.

**Dynamic patch scanning.** Daniel Lemire's August 2025 blog post "Why do we even need SIMD instructions?" measures glibc `memchr` at 84–175 GB/s on modern x86 desktops (174.75 GB/s at 8 KB input, 84.29 GB/s at 2 MB input, per PR #119 to his benchmark repo). On Cortex-X3/X4 at boot clock without optimized SIMD libc, plain pointer-loop `memchr` realistically runs at **~1–4 GB/s** (cache-resident, no NEON help in stock EDK2). Scanning a 3 MiB PE buffer with a single first-byte test is ~1–3 ms. The realistic cost is **N patterns × full-buffer scans**: with 5–10 patterns scanned across the whole buffer, expect **5–30 ms total**.

**Protocol hook install.** `gBS->ReinstallProtocolInterface` is documented in the UEFI Driver Writer's Guide §5.2.2 as performing disconnect→uninstall→install→reconnect (warning: "may induce reentrancy"). **On 160 BlockIo handles, blindly reinstalling on each is potentially expensive because of the reconnect step** — each reconnect may trigger PartitionDxe/Fat re-binding. If `BlockIoHook` only *inspects* handles (LocateHandleBuffer + GetProtocol), cost is ~1–5 ms. If it reinstalls on every BlockIo and causes PartitionDxe to re-attach, cost could spike to **50–200 ms**. **Audit this code path; it's the most variable line item.**

## Details: Ranked Cost Centers

Ranking is by **expected median wall-clock cost on a stock-clocked Cortex-X3 SM8550 in the regular (timeout) boot path**. All numbers are order-of-magnitude.

### #1 — ABL unwrap / decompression (cost center 4) — *est. 50–200 ms*

- **Why it dominates:** Only step that combines (a) reading several MiB from UFS at boot-stage throughput, (b) walking an FV/FFS structure, and (c) running a single-threaded LZMA decoder that the LZMA SDK README itself describes as integer-throughput-bound and not SIMD-amenable. EDK2's `LzmaCustomDecompressLib` is plain-C LZMA SDK.
- **Evidence:**
  - Format: XDA bootloader-modding thread documents the LZMA-compressed FV layout for Qualcomm ABL ("just split your abl at 0x3078 … the high piece is just the LZMA"). FV GUID matches EDK2's `IntelFrameworkModulePkg` LZMA section GUID. (Note: worthdoingbadly.com describes the Pixel 2 XL `taimen` MSM8998 inner FV as Gzip — confirm per OEM.)
  - Throughput: LZMA SDK README baseline ("20-30 MB/s on 2 GHz Core 2 … 1-2 MB/s on 200 MHz ARM"); scaled to Cortex-X3 at boot clock yields ~30–80 MB/s output. EDK2 source: `MdeModulePkg/Library/LzmaCustomDecompressLib`.
  - Partition I/O: 8 MiB `abl_a` partition (`0x800000` per OnePlus Nord `getvar`). Stock XBL reports `Flash Throughput, 262000 KB/s` on Linaro-published RB5 boot log.
  - Image size: 1996 KB compressed on OnePlus 8T flash log; 235 KB on Xiaomi 14 houji.
- **Optimization:**
  1. **Read only what's needed.** Parse the GPT entry to get the live size, then parse the FV header from the first 4 KiB to find the LZMA section offset/length, then `ReadBlocks` only those LBAs. This alone cuts the I/O from ~8 MiB to ~1–2 MiB.
  2. **Decompress directly into the destination buffer** instead of allocating an intermediate. EDK2's `LzmaCustomDecompressLib` already supports a single-pass `ExtractSection` interface.
  3. **Avoid double-decompression at `LoadImage` time** by handing `gBS->LoadImage` the already-unwrapped raw PE32 (skipping the FV wrapper).
- **Risk:** Low. Reading exact sizes is a pure win. Direct decompression buffer is a memory-management refactor but not a correctness risk if the destination has section-alignment slack.

### #2 — LogFs mount/rotation (cost center 3) — *est. 20–100 ms (called twice)*

- **Why it's likely large:** Two `LogFsInit()` calls, each potentially triggering `ConnectController` on a BlockIo handle and a FAT volume open. FAT mount reads BPB, FATs (often hundreds of KiB on a large partition), and root directory; log rotation copies file data and dirties the FAT — FAT writes are slow because every write touches multiple sectors.
- **Evidence:** EDK2 UEFI Driver Writer's Guide describes `ConnectController` as performing recursive driver binding and `start()` callbacks; `PartitionDxe` (`MdeModulePkg/Universal/Disk/PartitionDxe/Partition.c`) reads the partition table on every Start(). Once a SimpleFS handle exists, `OpenVolume` on FAT reads metadata before any file open.
- **Optimization (in priority order):**
  1. **Skip the second `LogFsInit()` entirely on the regular path** (handoff hypothesis #1). Likely worth **10–50 ms** alone.
  2. **Gate log rotation to debug/failure mode only** (handoff hypothesis #7). For a normal timeout-to-chainload path, never rotate.
  3. **Defer LogFs init until first log write**; on a normal silent boot, no logs need to be written — this can eliminate the FS mount cost entirely.
  4. **Use SerialPortLib (or a no-op log sink) as the default**, with FAT logging only enabled when an explicit "debug" GPIO/cmdline/EFI variable is set. The TianoCore `PerformanceLib` infrastructure (`PERF_FUNCTION_BEGIN/END` macros in `MdePkg/Include/Library/PerformanceLib.h`) is the canonical way to keep performance records in memory without filesystem touches.
- **Risk:** Low-to-medium. Skipping rotation is safe if log size is bounded by other means. The second `LogFsInit()` likely exists for a reason (recovery from an intermediate teardown) — audit the close that precedes it.

### #3 — LoadImage / StartImage handoff (cost center 7) — *est. 10–30 ms*

- **Why moderate:** Section copy + relocation walk + full-image I-cache flush. For a ~3 MiB PE this is dominated by memory bandwidth and the per-cacheline `ic ivau` flush.
- **Evidence:** EDK2 `MdeModulePkg/Core/Dxe/Image/Image.c` shows the relocation walk and explicit `InvalidateInstructionCacheRange(ImageAddress, ImageSize)` call. PE relocations on AArch64 use ADRP-style fixups with limited types — fast. The IC flush is per-cacheline.
- **Optimization:**
  1. **Hand `LoadImage` a SourceBuffer that is the raw PE (post-FV-unwrap)** so EDK2 doesn't re-process the FV.
  2. **Consider a custom mini-loader** that performs in-place relocation + IC flush and jumps directly, bypassing `gBS->LoadImage`'s policy/measure/profiler bookkeeping (DebugImageInfoTable registration, memory-profiler hooks). Typically saves 1–5 ms but **high risk** if any consumer depends on the image being registered.
  3. **Pre-flush only the executable sections**, not the whole image.
- **Risk:** Medium. Bypassing `LoadImage` is the highest-risk optimization on the list; can break crash debuggers and secure-boot policy. Recommend only doing the SourceBuffer-is-raw-PE fix.

### #4 — Protocol hook installation (cost center 6) — *est. 5–80 ms (highly variable)*

- **Why uncertain:** Cost depends entirely on whether `ProtocolHook_InstallAll` triggers `gBS->ConnectController` (or a `ReinstallProtocolInterface` chain) on every BlockIo handle. UEFI spec explicitly warns: ReinstallProtocolInterface "may induce reentrancy" because it disconnects and reconnects every consumer.
- **Evidence:** EDK2 UEFI Driver Writer's Guide §5.2.2 documents the disconnect→uninstall→install→reconnect sequence. UEFI 2.10 spec §7 confirms.
- **Optimization:**
  1. **Inspect-only mode by default.** If the hook only needs to *read* the BlockIo to identify the boot device, use `gBS->HandleProtocol` / `OpenProtocol(GET_PROTOCOL)` — no reinstall.
  2. **Target-specific install.** Reinstall only on the 1–2 handles you actually need to intercept (`abl_a`/`abl_b` or the boot device), not on all BlockIo handles.
  3. **Mode-gate diagnostic hooks** (handoff hypothesis #6).
  4. **Avoid `ReinstallProtocolInterface` if a wrapper protocol works.** Installing a new protocol on a new handle that shadows the original avoids the disconnect/reconnect storm.
- **Risk:** Medium. Changing hook semantics requires confirming downstream consumers still see what they expect.

### #5 — Dynamic patch scanning (cost center 5) — *est. 5–30 ms*

- **Why modest:** `memchr`-based first-byte scans over 1–3 MiB are fast even on a clamped CPU. Multiple patterns are additive but stay in cache.
- **Evidence:** Lemire's August 2025 benchmark shows glibc `memchr` at 174.75 GB/s at 8 KB input and 84.29 GB/s at 2 MB input on modern x86; on Cortex-X3 at ~1.5 GHz with stock EDK2 `BaseMemoryLib` (no SIMD) realistically ~1–3 GB/s per pass. Boyer-Moore-Horspool gives 2–4× on patterns ≥8 bytes.
- **Optimization (in priority order):**
  1. **Bound to PE `.text` sections** (handoff hypothesis #4). Walk PE section headers once, scan only `IMAGE_SCN_CNT_CODE` sections.
  2. **Single-pass combined scan** (hypothesis #5): if patterns are short, build a simple Aho-Corasick automaton or use first-byte memchr to anchor multi-pattern matching in one buffer pass.
  3. **Use anchor-first heuristics**: if patches target a function preceded by a unique constant, find the constant first; then verify the pattern in a small window.
- **Risk:** Low. PE-section bounding only loses matches in non-code regions, which shouldn't exist for code patches.

### #6 — Partition / BlockIo enumeration (cost center 1) — *est. 5–15 ms*

- **Why low:** `LocateHandleBuffer` is microseconds even at 160 handles. The Qualcomm-specific `UpdatePartitionEntries()` does perform LUN-level GPT reads (several LBAs per LUN), so cost is *some* I/O, not zero.
- **Evidence:** EDK2 `MdeModulePkg/Core/Dxe/Hand/Locate.c` (handle DB walk is a linear list); Qualcomm `QcomModulePkg/Library/BootLib/PartitionTableUpdate.c` does explicit `BlkIo->ReadBlocks` per LUN.
- **Optimization:**
  1. **Cache the handle list and the slot/partition→handle map** in CommonEarlyInit; never re-call `LocateHandleBuffer` afterward (handoff hypothesis #2).
  2. **Skip `UpdatePartitionEntries()` on chainload path** if you only need `abl_a`/`abl_b`; do a targeted lookup by name once.
  3. **Don't repeatedly resolve `abl_a`/`abl_b` by name** — resolve once, cache the handle and the start LBA.
- **Risk:** Low. Caching is safe as long as no partition-table mutation happens between enumeration and chainload.

### #7 — FV driver loading from current FV (cost center 2) — *est. <5 ms*

- **Why smallest:** The `gbl-chainload` FV is small (the listed libs are linked, not dispatched). With ≤2–3 actual driver FFS files, this is in the **single-digit ms range**.
- **Evidence:** EDK2 `MdeModulePkg/Core/Dxe/Dispatcher` shows the per-driver dispatch cost; `QcomModulePkg.dsc` on Codeaurora confirms most "lib" files are `LIBRARY_CLASS`, not separately built drivers.
- **Optimization:**
  1. **Audit the FV manifest** (`.fdf`) to confirm no unexpected drivers are dispatched. If only the application is in the FV, this call can be a no-op.
  2. **Skip the call entirely** on the regular path if no FV drivers exist.
- **Risk:** Very low.

## Overall "Do These First" Optimization List

In ranked order by expected wall-clock reclaimed × inverse risk:

1. **Eliminate the second `LogFsInit()` on the regular path.** Likely **10–50 ms**, trivial risk. Cache the handle from the first init, or restructure to never close it before chainload.
2. **Gate log rotation and (if possible) all LogFs activity to debug/failure mode.** Likely another **10–30 ms** on top of #1, very low risk.
3. **Read only the LZMA-section LBAs from `abl_a`, not the full 8 MiB partition.** Likely **15–40 ms** of I/O saved, low risk.
4. **Decompress LZMA directly into the destination buffer** and hand `LoadImage` the raw post-unwrap PE. Likely **5–20 ms** saved, plus avoids any double-decompression risk.
5. **Confirm `ProtocolHook_InstallAll` does NOT use `ReinstallProtocolInterface` over all 160 BlockIo handles.** If it does, switch to inspect-only or shadow-handle pattern. Potential save: **5–80 ms** on outlier devices, medium risk; high audit value.
6. **Cache BlockIo + partition enumeration results from CommonEarlyInit** (handoff hypothesis #2). Likely **3–10 ms**, very low risk.
7. **Bound dynamic patch scans to PE `.text` sections and consolidate multiple passes.** Likely **3–15 ms**, low risk.
8. **Audit `LoadDriversFromCurrentFv`** and skip if FV has no dispatched drivers. Likely **0–5 ms**, trivial risk.

If you must pick just **three** things to do this week, do #1, #2, and #3. Together they should reclaim **35–120 ms** with negligible reliability impact.

## Easy Instrumentation Tips Beyond the Handoff

- **AArch64 cycle counter.** `mrs x0, cntvct_el0` reads the virtual count register; combined with `mrs x1, cntfrq_el0` (frequency in Hz) you get a high-resolution wall clock without needing the EDK2 TimerLib. Both are readable at EL2/EL1 by default on Snapdragon (and at EL0 if `CNTKCTL_EL1.EL0VCTEN` is set, which it normally is). CNTFRQ_EL0 on Qualcomm is typically 19.2 MHz (≈52 ns per tick) — plenty of resolution for ms-scale measurements.
- **EDK2 PerformanceLib macros.** `PERF_FUNCTION_BEGIN()` / `PERF_FUNCTION_END()` from `MdePkg/Include/Library/PerformanceLib.h` already record entries in a memory ring buffer; you don't need a filesystem. Pair with a `DEBUG((DEBUG_INFO, "perf: %a=%ld us\n", ...))` at the end for serial-only output.
- **DEBUG-print barriers.** Each `DEBUG((DEBUG_INFO, ...))` on Qualcomm UEFI goes to the serial UART (or RAM log if UART is muxed off). On flagships with no UART, DEBUG prints are nearly free; on dev boards they can serialize boot. Confirm your build's `DebugLib` is `BaseDebugLibNull` in RELEASE.
- **One-line ABL-stage marker.** Bracket each of the 7 stages with a `cntvct_el0` capture and dump the deltas as a single hex string to a fixed RAM address (or an EFI variable) before chainloading. The next stage can read it back via `GetVariable`.
- **Use the Qualcomm SBL1 timestamps.** Stock XBL/SBL1 already emits `B - <us> - <stage>` and `D - <delta> - <stage>` lines (visible in Linaro RB5 logs as `B - 1011898 - SBL1, End` / `D - 937631 - SBL1, Delta`). On devices with a serial console exposed, capture these via grabserial to anchor the absolute timeline before your `gbl-chainload` measurements.

## Caveats

- **All wall-clock figures are estimates**, not measured `gbl-chainload` numbers. The `gbl-chainload` repository itself is unavailable; only the `gbl_root_canoe` security-research repo (131 stars, C-heavy, GPL-3.0, built on EDK2 with `extractfv.py`/`patch_abl.c` tooling) is public, and it does not publish boot-time telemetry. The handoff document was the primary structural source.
- **CPU clock state at ABL time is the largest single uncertainty.** If the platform leaves the boot CPU at peak frequency (~3 GHz), every CPU-bound estimate (LZMA, memchr scan, IC flush) is roughly half what's stated. If it's clamped to ~600–800 MHz (common, per AOSP boot-time-opt docs), they're 2× worse.
- **ABL format may not be LZMA on every OEM.** Pixel-class Qualcomm ABLs (Snapdragon 835 `taimen`) are documented as Gzip-wrapped on worthdoingbadly.com; modern OnePlus/Xiaomi ABLs are documented by XDA modders as LZMA. Verify by sniffing the FV header GUID before assuming.
- **`ReinstallProtocolInterface` cost is bimodal.** Microseconds if nobody is consuming the protocol, hundreds of ms if many drivers are bound. Without the actual `ProtocolHookLib` source, this is genuinely unknown — the cost-center most worth instrumenting first.
- **No public benchmark of EDK2 LZMA decompression at boot stage on Snapdragon** exists; the 30–80 MB/s figure is interpolated from the LZMA SDK README's historical ARM numbers scaled by ~10–25× to modern Cortex-X3, then halved for boot-clock derating. Treat as ±2×.
- **Partition-size precision:** No public `fastboot getvar partition-size:abl_a` was found for an OnePlus 11/12/13 retail unit specifically; the 8 MiB figure is from the OnePlus Nord (`0x800000`) and matches Qualcomm reference designs. No SM8550-specific S-/B-/D- ABL timestamp dump was located either — the 938 ms SBL1 figure is from a Linaro RB5 boot log, an older but architecturally similar platform.
