# Regular Boot Loading-Hit Research Handoff

**Scope:** `ROOT_REPO` (`~/gbl-chainload`) regular boot path only.

This handoff is for researching the largest boot-time costs in the normal
chainload path:

```text
stock ABL -> gbl-chainload.efi -> patched ABL -> normal Android boot
```

Do **not** focus on FastbootLib/debug-command paths unless they affect the
regular timeout-to-chainload path. Fastboot menu/`oem` command work is a separate
surface.

## Primary question

Which operations in `gbl-chainload` add the most wall-clock time before patched
ABL starts, and which ones can be reduced or skipped without making normal boot
less reliable?

## Current regular boot path

Entry point:

- `GblChainloadPkg/Application/GblChainload/Entry.c`

Ordered flow:

1. `GblChainloadEntry()`
2. `CommonEarlyInit()`
   - `DeviceInfoInit()`
   - `EnumeratePartitions()`
   - `UpdatePartitionEntries()`
   - `SignalSDDetection()`
   - `LoadDriversFromCurrentFv(ImageHandle)`
   - `LogFsInit()`
3. In `GBL_AUTO=0` regular mode, timeout path calls `TryChainLoad()`.
4. `TryChainLoad()` calls `BootFlowChainLoad()`.
5. `BootFlowChainLoad()` in `GblChainloadPkg/Application/GblChainload/BootFlow.c`:
   - reopens logfs with `LogFsInit()`
   - resolves active `abl_a` / `abl_b`, fallback `abl`
   - calls `AblUnwrap_LoadFromPartition()`
   - calls `DynamicPatchLib_EnsureInit()` and `DynamicPatch_Apply()`
   - calls `ProtocolHook_InstallAll()`
   - closes logfs
   - calls `gBS->LoadImage()` on patched ABL PE
   - calls `gBS->StartImage()` to hand off to patched ABL

## Suspected loading / boot-time cost centers

### 1. Partition and BlockIo enumeration

Relevant paths:

- `Entry.c`: `EnumeratePartitions()`, `UpdatePartitionEntries()`
- `GblChainloadPkg/Library/LogFsLib/Mount.c`
- `GblChainloadPkg/Library/AblUnwrapLib/AblUnwrapLib.c`
- `GblChainloadPkg/Library/ProtocolHookLib/BlockIoHook.c`

Why it may cost time:

- Qualcomm devices may expose ~160 partitions/BlockIo handles.
- Several pieces of the flow scan handles independently.
- `MAX_NUM_PARTITIONS`/partition table sizing issues can affect correctness and
  scan behavior.

Research questions:

- On comparable Qualcomm EDK2/LK2 bootloaders, how expensive is repeated
  `LocateHandleBuffer(ByProtocol, BlockIo)` during early boot?
- Does `UpdatePartitionEntries()` scale linearly with partition count?
- Can handles/partition names be cached once in `CommonEarlyInit()` and reused
  by logfs, ABL unwrap, and BlockIo hook install?

Instrumentation points:

- before/after `EnumeratePartitions()`
- before/after `UpdatePartitionEntries()`
- count handles returned by each `GetBlkIOHandles()` / `LocateHandleBuffer()`
- total time spent scanning BlockIo in `InstallBlockIoHook()`

### 2. FV driver loading from current FV

Relevant path:

- `Entry.c`: `LoadDriversFromCurrentFv(ImageHandle)`

Why it may cost time:

- Loads drivers from the same FV as `gbl-chainload.efi`.
- May trigger driver binding / controller connection work.

Research questions:

- What drivers are actually loaded by `LoadDriversFromCurrentFv()` in this
  build?
- Are all of them required for regular boot, or only for Fastboot/logfs/fallback?
- Does this call dominate pre-chainload time compared to ABL unwrap/patching?

Instrumentation points:

- timestamp before/after `LoadDriversFromCurrentFv()`
- log each loaded driver GUID/name if possible
- compare a build that skips it against current behavior on regular boot

### 3. LogFS mount and rotation

Relevant paths:

- `Entry.c`: initial `LogFsInit()`
- `BootFlow.c`: second `LogFsInit()` reopen before chainload session
- `GblChainloadPkg/Library/LogFsLib/Mount.c`
- `GblChainloadPkg/Library/LogFsLib/Rotation.c`

Why it may cost time:

- Mount work may scan BlockIo handles and connect controllers.
- `LogFsRotateUefiLog()` may touch filesystem metadata and copy/rotate logs.
- `BootFlowChainLoad()` reopens logfs after Fastboot paths; regular timeout path
  may still pay an avoidable second-init cost depending on current state.

Research questions:

- Is the second `LogFsInit()` in `BootFlowChainLoad()` necessary for regular
  boot, or only after `EnterFastboot()` has closed logfs?
- What is the cost of `ConnectController()` + `SimpleFileSystem->OpenVolume()`
  on this platform?
- Can regular boot avoid rotation or defer it until failure/debug builds?

Instrumentation points:

- total `LogFsInit()` time in `Entry.c`
- total `LogFsInit()` time in `BootFlow.c`
- split `Mount.c` into handle scan, connect controller, open volume
- `LogFsRotateUefiLog()` duration and bytes moved

### 4. ABL unwrap / decompression

Relevant path:

- `GblChainloadPkg/Library/AblUnwrapLib/AblUnwrapLib.c`

Main operations:

- locate active `abl_a` / `abl_b` or fallback `abl`
- read the partition
- find FV/FFS sections
- unwrap PE/TE image
- LZMA decompress when needed

Why it may cost time:

- Full partition read can be expensive.
- FV/FFS section walking may scan a large buffer.
- LZMA decompression can be a large one-time cost.

Research questions:

- On target builds, is ABL usually LZMA-wrapped, raw PE32, TE, or nested FV?
- How large are `abl_a`/`abl_b` partitions and actual ABL payloads?
- Can the code read only the used bytes instead of the full partition?
- Can section offsets be cached or recognized by known signatures per build?
- What is typical LZMA decompression cost on the device CPU at boot stage?

Instrumentation points:

- partition locate time
- `ReadBlocks()` time and byte count
- FV scan time
- FFS/section walk time
- LZMA `GetInfo` and `Decompress` time
- output PE size

### 5. Dynamic patch scanning

Relevant paths:

- `GblChainloadPkg/Library/DynamicPatchLib/PatchTable.c`
- `GblChainloadPkg/Library/DynamicPatchLib/Internal/PatchEngine.c`
- `GblChainloadPkg/Library/DynamicPatchLib/Internal/ScanLib.c`
- `GblChainloadPkg/Library/DynamicPatchLib/Internal/PeSections.c`
- `GblChainloadPkg/Library/DynamicPatchLib/universal/universal.c`
- `GblChainloadPkg/Library/DynamicPatchLib/mode_1/mode_1.c`
- `GblChainloadPkg/Library/DynamicPatchLib/oem/oneplus_canoe.c`

Why it may cost time:

- Signature scans may walk large PE buffers.
- Bounded-section checks reduce false positives but still add work.
- Multiple patches may independently scan overlapping regions.

Research questions:

- Which patch patterns are mandatory and which are diagnostic/optional?
- How many bytes are scanned per patch?
- Can all signatures be constrained to executable sections before scan?
- Can multiple patterns be searched in one pass?
- Can offsets be cached per ABL build hash/version?

Instrumentation points:

- total `DynamicPatchLib_EnsureInit()` time
- total `DynamicPatch_Apply()` time
- per-patch scan time, matched offset, bytes scanned
- PE section parse time
- count mandatory/optional misses

### 6. Protocol hook installation

Relevant paths:

- `GblChainloadPkg/Library/ProtocolHookLib/InstallAll.c`
- `GblChainloadPkg/Library/ProtocolHookLib/BlockIoHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/QseecomHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/SpssHook.c`
- `GblChainloadPkg/Library/ProtocolHookLib/Mode1Overlay.c`
- `GblChainloadPkg/Library/ProtocolHookLib/UniversalBaseline.c`

Why it may cost time:

- Hook install may scan protocol handles.
- `BlockIoHook` is likely the largest because it may inspect every BlockIo
  handle.
- Mode-1 overlay hooks add fakelock/verified-boot behavior.

Research questions:

- Which hooks are required for mode-0 regular boot vs mode-1 regular boot?
- How expensive is `BlockIoHook` across ~160 partitions?
- Can hooks be installed lazily only when patched ABL queries relevant
  protocols?
- Can BlockIoHook restrict itself to partitions/protocols actually needed by
  the mode?

Instrumentation points:

- total `ProtocolHook_InstallAll()` time
- per-hook install time and outcome
- BlockIo handle count and per-handle cost
- number of protocol replacements performed

### 7. LoadImage / StartImage handoff

Relevant path:

- `BootFlow.c`: `gBS->LoadImage()` and `gBS->StartImage()`

Why it may cost time:

- `LoadImage()` on the in-memory patched ABL may validate/relocate/process PE.
- `StartImage()` transfers to patched ABL; anything after this belongs to the
  patched ABL / stock boot path, not `gbl-chainload` itself.

Research questions:

- How large is the in-memory PE passed to `LoadImage()`?
- Does UEFI PE relocation dominate this step?
- Does cache maintenance happen inside `LoadImage()` or before `StartImage()`
  on this platform?

Instrumentation points:

- before/after `LoadImage()`
- immediately before `StartImage()`
- first observable log line from patched ABL after `StartImage()`

## Existing build dependencies relevant to runtime cost

From the current root build, linked libraries include many EDK2/Qcom libs. Not
all linked libs are active on the regular boot path. Prioritize runtime
measurement, not link-list pruning alone.

Most relevant to regular boot latency:

- `GblChainloadPkg/Library/AblUnwrapLib`
- `GblChainloadPkg/Library/DynamicPatchLib`
- `GblChainloadPkg/Library/ProtocolHookLib`
- `GblChainloadPkg/Library/LogFsLib`
- `MdeModulePkg/Library/LzmaCustomDecompressLib`
- `QcomModulePkg/Library/LoadFVLib`
- `MdePkg` UEFI boot/runtime service wrappers
- `ArmPkg/Library/ArmCacheMaintenanceLib`

Likely less relevant to regular timeout-to-chainload unless pulled indirectly by
hooks/fallback paths:

- `FastbootLib`
- `AvbLib`
- `OpenDice`
- `AesLib`
- `Lz4Lib`
- `Zlib`

## Data to collect

Ask the research agent to gather or infer real-world data for:

1. Qualcomm EDK2 / ABL partition enumeration cost on devices with 100-200 GPT
   entries.
2. UEFI `ConnectController()` + FAT/SimpleFS mount cost on Android bootloader
   platforms.
3. LZMA decompression performance on comparable ARM cores at bootloader clock
   state.
4. PE/TE image load/relocation cost for a several-MiB ABL image.
5. Cost of full-buffer byte-pattern scanning over 1-8 MiB images in pre-OS C.
6. Whether repeated BlockIo/protocol handle scans are known bottlenecks in EDK2.
7. Known best practices for low-latency bootloader logging without mounting a
   filesystem on every boot.

## Proposed local instrumentation plan

Use a simple monotonic timestamp helper around each stage and write through the
existing `GBL_INFO`/logfs channel.

Suggested stage markers:

```text
T+... Entry.DeviceInfoInit
T+... Entry.EnumeratePartitions
T+... Entry.UpdatePartitionEntries
T+... Entry.LoadDriversFromCurrentFv
T+... Entry.LogFsInit.initial
T+... BootFlow.LogFsInit.reopen
T+... BootFlow.AblUnwrap.total
T+... AblUnwrap.partition_scan
T+... AblUnwrap.readblocks bytes=N
T+... AblUnwrap.fv_scan
T+... AblUnwrap.section_walk
T+... AblUnwrap.lzma_decompress in=N out=N
T+... BootFlow.DynamicPatch.total
T+... DynamicPatch.patch name=... scanned=N result=...
T+... BootFlow.ProtocolHook.total
T+... ProtocolHook.BlockIo handles=N installed=N
T+... BootFlow.LoadImage
T+... BootFlow.StartImage.enter
```

Collect at least:

- cold boot regular mode-0
- warm reboot regular mode-0
- cold boot mode-1
- warm reboot mode-1
- debug/logfs-enabled build vs logging-minimal build

## Candidate optimization hypotheses

Rank these only after measurement:

1. Avoid second `LogFsInit()` in `BootFlowChainLoad()` on the regular path when
   logfs was not closed by Fastboot.
2. Cache BlockIo/partition enumeration results from `CommonEarlyInit()` for
   `LogFsLib`, `AblUnwrapLib`, and `BlockIoHook`.
3. Reduce ABL reads from full partition to exact occupied image size if a safe
   header/footer tells us the bound.
4. Bound dynamic patch scans to executable PE sections before each pattern scan.
5. Combine multiple patch scans over the same executable section into one pass.
6. Make optional/diagnostic hooks lazy or mode-gated.
7. Gate log rotation to debug/failure modes if it is expensive.

## Non-goals

- Do not measure `fastboot oem boot-efi`, `oem escape`, or Fastboot menu actions
  for this handoff unless they change the regular timeout path.
- Do not optimize by removing correctness-critical mode-1 hooks until their
  actual cost and functional requirement are demonstrated.
- Do not replace durable boot diagnostics blindly; first measure logfs overhead
  and decide what can be deferred.
