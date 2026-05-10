# AVB façade graft + libavb-status fastboot interface

**Date**: 2026-05-10
**Status**: Design draft; awaiting user review.
**Working directory**: `/home/vivy/gbl-chainload` (v2 repo, `main` at `0b14cca` after Plan 2 + post-Plan-2 fixes).

## Context

Plan 2 (`v2.0.0-plan2-mode0-logfs-patch9v2`) shipped mode-1 + patch9 v2 (3-site). Device-validated on infiniti EU 16.0.5.703: ABL emits clean locked/green state (KM 0x208 `isUnlocked=0, color=0`, real OEM pubkey, `oplusboot.verifiedbootstate=green`, `set_boot_info_to_rpmb: lock_state:1`). Custom recovery boots with this state.

But normal boot to Android system fails when custom recovery is flashed alongside LKM-patched init_boot. Per `docs/re/aosp-early-avb-bootflow.md`: `first_stage_init.cpp::AvbHandle::Open()` re-walks vbmeta on disk via libavb_user. Recovery's `chain_partition_descriptor` validation reads the chained partition's embedded vbmeta header and verifies the signature against the descriptor's stored public key. Custom recovery has no (or wrong-signed) embedded vbmeta header at the tail → signature check fails → init returns `AvbSlotVerifyResult::ERROR_VERIFICATION` → `SetUpDmVerity()` fails → init falls back to recovery mode.

This spec covers two tightly-related sub-projects:

- **Plan 3a — AVB façade graft**: graft stock embedded vbmeta into the user's custom recovery/dtbo image so first_stage_init's chain check passes. Three tracks of tooling (host script + on-device fastboot oem command + flashable ZIP). Direct unblocker for the custom-HLOS scenario the user reports.
- **Plan 3d — libavb-status fastboot interface**: `getvar` and `oem` surface for AVB-state diagnostics. Lets operators see vbmeta digest, per-partition descriptor type, expected vs computed hashes, and overall status. Standalone utility, share an `AvbParseLib` with Plan 3a.

Both gated behind build flags (default ON for mode-1+, OFF for mode-0 to keep the minimal mode minimal).

## Section 1 — Partition coverage

### In scope

- `recovery_a` / `recovery_b` (chain_partition_descriptor per `docs/re/avb-descriptor-findings-eu-16.0.5.703.md`)
- `dtbo` / `dtbo_a` / `dtbo_b` (chain_partition_descriptor; user-replaceable)

### Explicitly out of scope

- `vbmeta_system`, `vbmeta_vendor` — also chain_partition_descriptor, but OEM-signed, rarely user-replaced. Untouched.
- `init_boot`, `vendor_boot`, `boot` — hash_descriptor partitions. Not addressed here.
- Userspace `vbmeta.img` flag manipulation (`--disable-verification`, `--disable-verity`). Not in our payload.
- Mode-2 KM-payload spoof (Play Integrity / SafetyNet). Separate plan.
- Init_boot patcher tooling (LKM/AK3/KSUN already do what's needed). Out of scope.

### Why hash-descriptor partitions are out of scope

The user's working setup (stock recovery + LKM-patched init_boot → system boots) works because their `vbmeta.img` was re-flashed (by their Magisk/KSUN-style installer) with the `--disable-verification` flag set in the AVB image flags. AOSP's userspace AVB honors that flag and tolerates hash-descriptor mismatches. Mode-1 + patch9 v2 ensures ABL's libavb passes through to populated SlotData; ABL's cmdline emits green/locked. Userspace then sees `disable-verification` and accepts the on-disk content despite the hash mismatch.

**None of this is gbl-chainload's doing.** It's the user's pre-existing vbmeta flag setup. We don't ship vbmeta.img modification tooling and we don't need to.

### Why recovery's chain validation isn't covered by `disable-verification`

The `disable-verification` flag skips dm-verity setup and tolerates hash-descriptor mismatches in the cmdline-state path. It does NOT bypass `chain_partition_descriptor` signature verification. Chain validation requires the chained partition's embedded vbmeta header to be signed by a key that matches the descriptor's stored public key (or hash). With custom recovery, the embedded vbmeta header is missing or signed with the wrong key → chain check fails regardless of vbmeta flags.

That's the specific hole this spec fills.

## Section 2 — Plan 3a tooling (three tracks)

All three tracks share one core operation: **copy the stock partition's embedded vbmeta region** (last `partition_size - original_image_size` bytes, anchored by an `AvbFooter` at the very end) **into the same byte-region of the user's custom partition image**. The user's payload (kernel, ramdisk, fdt) above that region stays unchanged.

```
Custom recovery image (input):  [ user kernel + ramdisk + 0x10000 padding | custom_garbage_footer? ]
Stock recovery image (donor):   [ stock kernel + ramdisk + 0x00000 padding | stock_avb_footer + stock_vbmeta_header + stock_aux_data ]
                                                                            ↓
Grafted custom recovery (out):  [ user kernel + ramdisk + 0x10000 padding | stock_avb_footer + stock_vbmeta_header + stock_aux_data ]
```

The byte-region boundary is identified by parsing the donor's `AvbFooter` (last 64 bytes of partition, magic `"AVBf"`). The graft replaces bytes `[partition_size - footer.vbmeta_offset - footer.vbmeta_size, partition_size]` with the donor's equivalent region.

### Track 1 — Host Python script `scripts/graft-vbmeta.py`

Pure stdlib Python (`struct` + file I/O). No on-device dependency. ~150 lines.

```bash
./scripts/graft-vbmeta.py \
  --partition recovery \
  --stock /path/to/stock_recovery.img \
  --custom /path/to/twrp.img \
  --out /path/to/grafted.img
```

Validates donor footer magic (`"AVBf"`), parses footer, extracts the vbmeta region, validates header magic (`"AVB0"`), splices into custom image at the same byte-offset-from-end, writes output. Refuses if any magic check fails.

User runs this offline, then `fastboot flash recovery_a grafted.img` (and `_b` if A/B).

### Track 2 — On-device `fastboot oem graft-and-flash <partition>`

Workflow:

```bash
fastboot stage path/to/twrp.img
fastboot oem graft-and-flash recovery
# (dry-run prints expected operation; no write yet)
fastboot oem graft-and-flash recovery commit
# (atomic write to active slot's recovery partition)
```

Behavior:

1. **Source validation.** Read the active slot's partition (e.g. `recovery_a` if `slot=a`) via BlockIo. Read the last 64 bytes. Require magic `"AVBf"`. Parse `AvbFooter` (uses `AvbParseLib`, §3). Validate `vbmeta_offset` and `vbmeta_size` are within partition. Refuse with explicit diagnostic if invalid (e.g. user already grafted; partition was custom-flashed without vbmeta).
2. **Staged-image validation.** Confirm the staged buffer is at least the partition size. Verify the buffer has plausible image structure for the target partition: recovery → Android boot image magic `"ANDROID!"` at offset 0; dtbo → fdt magic `0xD00DFEED` at offset 0 (or DTBO entry at offset 0 for multi-entry dtbo containers). Refuse if magic mismatch.
3. **Header-region validation.** Parse `AvbVBMetaImageHeader` at the donor's `vbmeta_offset` from end. Require magic `"AVB0"`. Verify algorithm field is one of the standard AVB algorithm types. Verify `auth_data_block_size + aux_data_block_size + sizeof(header)` equals `vbmeta_size`. Refuse if any sanity check fails.
4. **Dry run by default.** First invocation (`fastboot oem graft-and-flash <part>`) prints:
   ```
   donor footer @ partition[<size> - 64]: vbmeta_offset=<N> vbmeta_size=<M> orig_image_size=<K>
   donor vbmeta header: algo=<X> aux_size=<Y> auth_size=<Z> flags=<F>
   target staged: <staged_size> bytes; magic=<MM>
   target partition: <part>_<slot> @ blkdev <D>
   would write <staged_size> bytes; vbmeta region replaced with donor's <vbmeta_size+64> bytes
   ```
   No write. User reviews, then issues `fastboot oem graft-and-flash <part> commit` to actually write.
5. **Atomic write.** Single `BlockIo->WriteBlocks()` call against the partition handle. Atomic at the BlockIo layer.
6. **Optional `--backup`.** When `commit` is invoked with `--backup`, save the original (pre-graft) vbmeta region (~256-byte tail of partition) to logfs at `/logfs/avb-graft-backup-<part>-<timestamp>.bin` before write. Default OFF (off-by-default to keep logfs clean). User can restore with a one-liner if needed.

Footprint: ~150 lines new C in our edk2 fork's FastbootCmds + AvbParseLib calls. Estimated +15-20K binary.

### Track 3 — Flashable ZIP (custom recovery installer)

A flashable ZIP that's run from inside an existing custom recovery (TWRP, OFOX, etc.). Layout:

```
gbl-graft-installer.zip
├── META-INF/com/google/android/
│   ├── update-binary           # AnyKernel3-style entry; bash script
│   └── updater-script          # 'set_progress' annotations only
├── recovery_custom.img         # user-provided custom recovery image
├── dtbo_custom.img             # user-provided custom dtbo image (optional)
└── tools/
    ├── graft-vbmeta            # static-linked aarch64 binary (compiled from same source as Track 1's Python — actually a small C/Go binary for recovery's busybox-restricted env)
    └── busybox                 # standard recovery toolset
```

`update-binary` script:

1. Read on-disk active slot's stock recovery partition into `/tmp/stock_recovery.img` via `dd`.
2. Run `tools/graft-vbmeta --partition recovery --stock /tmp/stock_recovery.img --custom recovery_custom.img --out /tmp/grafted_recovery.img`.
3. `dd` `/tmp/grafted_recovery.img` → `/dev/block/by-name/recovery_<slot>`.
4. Repeat for dtbo if `dtbo_custom.img` present.

ZIP is user-assembled (user adds their custom images before zipping) — we ship the installer template + tools. User maintains the ZIP across OTAs.

OTA recovery flow:

```
OTA arrives → installs to inactive slot → reboot → device on new (stock) slot.
User boots into custom recovery (still on the previous active slot? or stays on new stock slot?).
User flashes gbl-graft-installer.zip via custom recovery.
update-binary reads on-disk stock recovery (now the new OTA's stock) → grafts → flashes.
Reboot to system. Custom recovery present + system boots.
```

### OTA persistence strategy

**No persistent cache for stock vbmeta.** The on-disk stock partition (post-OTA, pre-customize) IS the freshest source. Workflow:

- After OTA: device reboots into new stock slot. The new slot's recovery partition contains stock recovery (just-installed by OTA).
- User runs Track 1 (host script) or Track 3 (ZIP) to graft custom recovery using the on-disk stock as donor.
- Optionally Track 2 (fastboot oem) for first-time setup.

We avoid:
- Caching stock vbmeta in our payload (stale across OTAs unless we recompile).
- Caching in logfs (stale across OTAs; doesn't help).
- Parsing the OTA package format (varies by OEM; brittle).

Re-grafting per OTA is a small operational cost in exchange for architectural simplicity.

## Section 3 — `AvbParseLib` (custom parser)

A minimal AVB structure parser, our own — no link dependency on libavb. Used by Plan 3a Track 2 (graft validation) and Plan 3d (status reporting).

**Files:**
- `GblChainloadPkg/Include/Library/AvbParseLib.h`
- `GblChainloadPkg/Library/AvbParseLib/AvbParse.c`
- `GblChainloadPkg/Library/AvbParseLib/AvbParseLib.inf`

**Surface:**

```c
typedef struct {
  UINT64  OriginalImageSize;
  UINT64  VbmetaOffset;        /* bytes from start of partition */
  UINT64  VbmetaSize;
  UINT32  FooterMajorVersion;
  UINT32  FooterMinorVersion;
} GBL_AVB_FOOTER;

typedef struct {
  UINT32  AvbMajorVersion;
  UINT32  AvbMinorVersion;
  UINT64  AuthenticationDataBlockSize;
  UINT64  AuxiliaryDataBlockSize;
  UINT32  AlgorithmType;       /* AVB_ALGORITHM_TYPE_* */
  UINT64  HashOffset;
  UINT64  HashSize;
  UINT64  SignatureOffset;
  UINT64  SignatureSize;
  UINT64  PublicKeyOffset;
  UINT64  PublicKeySize;
  UINT64  PublicKeyMetadataOffset;
  UINT64  PublicKeyMetadataSize;
  UINT64  DescriptorsOffset;
  UINT64  DescriptorsSize;
  UINT64  RollbackIndex;
  UINT32  Flags;
  UINT32  RollbackIndexLocation;
  CHAR8   ReleaseString[48];
} GBL_AVB_VBMETA_HEADER;

typedef enum {
  GblAvbDescPropertyTag      = 0,
  GblAvbDescHashtreeTag      = 1,
  GblAvbDescHashTag          = 2,
  GblAvbDescKernelCmdlineTag = 3,
  GblAvbDescChainPartitionTag = 4,
} GBL_AVB_DESCRIPTOR_TAG;

/* Read AvbFooter from the last 64 bytes of a partition buffer.
   Returns EFI_NOT_FOUND if magic 'AVBf' missing or version unsupported. */
EFI_STATUS
EFIAPI
AvbParse_Footer (
  IN  CONST UINT8        *Partition,
  IN  UINT64              PartitionSize,
  OUT GBL_AVB_FOOTER     *FooterOut
  );

/* Parse AvbVBMetaImageHeader from a vbmeta byte region.
   Returns EFI_NOT_FOUND if magic 'AVB0' missing or version unsupported. */
EFI_STATUS
EFIAPI
AvbParse_VbmetaHeader (
  IN  CONST UINT8                *Vbmeta,
  IN  UINT64                      VbmetaSize,
  OUT GBL_AVB_VBMETA_HEADER      *HeaderOut
  );

/* Iterator over descriptors in the auxiliary data block.
   On first call: pass *Cursor=0.  Subsequent calls advance Cursor.
   Returns EFI_END_OF_MEDIA when no more descriptors. */
EFI_STATUS
EFIAPI
AvbParse_NextDescriptor (
  IN     CONST UINT8                *AuxBlock,
  IN     UINT64                      AuxSize,
  IN OUT UINT64                     *Cursor,
  OUT    GBL_AVB_DESCRIPTOR_TAG     *TagOut,
  OUT    CONST UINT8               **DescriptorOut,
  OUT    UINT64                     *DescriptorLenOut
  );

/* Parse a hash descriptor's fields (partition_name, salt, digest). */
EFI_STATUS
EFIAPI
AvbParse_HashDescriptor (
  IN  CONST UINT8  *Descriptor,
  IN  UINT64        DescriptorLen,
  OUT CONST UINT8 **PartitionNameOut,
  OUT UINT32       *PartitionNameLenOut,
  OUT CONST UINT8 **DigestOut,
  OUT UINT32       *DigestLenOut
  );

/* Parse a chain_partition descriptor's fields. */
EFI_STATUS
EFIAPI
AvbParse_ChainPartitionDescriptor (
  IN  CONST UINT8  *Descriptor,
  IN  UINT64        DescriptorLen,
  OUT CONST UINT8 **PartitionNameOut,
  OUT UINT32       *PartitionNameLenOut,
  OUT CONST UINT8 **PublicKeyOut,
  OUT UINT32       *PublicKeyLenOut
  );
```

Implementation: ~400 lines C99, big-endian-aware (AVB structures are stored big-endian on disk). Field layouts derived from `external/avb/libavb/avb_footer.h` and `avb_vbmeta_image.h` in the local AOSP checkout (reference, not link target).

Host-testable: `tests/avb/test_avbparse.c` constructs synthetic AVB structures (footer + header + descriptors) and verifies the parser returns expected fields. Following the same TDD pattern as ScanLib in plan 1.

Footprint: 4-8K binary contribution.

## Section 4 — Plan 3d libavb-status fastboot interface

Two interaction shapes, both backed by `AvbParseLib` plus a SHA256 implementation (which we already have via EDK-II's `BaseCryptLib` or our own).

### `fastboot getvar` (read-only, scriptable, single value)

```
fastboot getvar vbmeta:digest                  → 64 hex chars (SHA256 of vbmeta_a || vbmeta_b on active slot)
fastboot getvar vbmeta:slot                    → "a" or "b"
fastboot getvar vbmeta:<part>:descriptor-type  → "hash" | "chain" | "hashtree" | "none"
fastboot getvar vbmeta:<part>:expected         → 64 hex chars (descriptor's stored hash) or "n/a"
fastboot getvar vbmeta:<part>:computed         → 64 hex chars (SHA256 over partition content) or "n/a"
fastboot getvar vbmeta:<part>:status           → "ok" | "mismatch" | "unsigned" | "n/a"
```

`<part>` ∈ `boot, init_boot, vendor_boot, recovery, dtbo, vbmeta_system, vbmeta_vendor` (devices may have more or fewer; our list is a per-OEM enum in our payload, with `none` returned for unknown).

`status`:
- `ok` — descriptor type is hash and computed == expected, OR descriptor type is chain and chained partition's signature verifies.
- `mismatch` — descriptor type is hash and computed != expected.
- `unsigned` — descriptor type is chain and the chained partition has no AVB footer / wrong magic.
- `n/a` — partition has no descriptor in vbmeta.

### `fastboot oem vbmeta-status` (multi-line report)

```
$ fastboot oem vbmeta-status
INFO partition       descriptor    expected                  computed                  status
INFO recovery_a      chain         8d897f624...              <computed_chain_pk_hash>  ok
INFO dtbo            chain         8d897f624...              <computed_chain_pk_hash>  ok
INFO init_boot_a     hash          a1b2c3d4e...              a1b2c3d4e...              ok
INFO vendor_boot_a   hash          5f4e3d2c1...              5f4e3d2c1...              ok
INFO ...
INFO vbmeta digest   sha256        <vbmeta_a digest>
INFO disable-verity   flag         ON
INFO disable-verification flag     ON
```

Multi-line output. Useful when:
- Diagnosing why first_stage_init falls into recovery mode (which partition's hash is wrong).
- Confirming a graft worked (post-graft `recovery_a` should show `chain ok`).
- Confirming user's `vbmeta.img` flags (lets us see if their setup has `disable-verification` ON, explaining why hash partitions tolerate mismatches).

### Footprint

- `AvbParseLib`: ~4-8K (shared with Plan 3a).
- SHA256: already in EDK-II MdePkg's `BaseCryptLib` (or use a small standalone impl ~3K).
- New fastboot `getvar` handlers: ~3-5K in edk2 fork's `FastbootCmds.c`.
- New `oem vbmeta-status` handler: ~2K.

Total Plan 3d delta: ~12-18K.

## Section 5 — Build flag gating + size budget

### Build flags

- `GBL_AVB_GRAFT_FASTBOOT=1` — enables Track 2 (`fastboot oem graft-and-flash`). Default ON for mode-1+, OFF for mode-0.
- `GBL_AVB_STATUS=1` — enables Plan 3d (`getvar vbmeta:*`, `oem vbmeta-status`). Default ON for mode-1+, OFF for mode-0.

`AvbParseLib` is built unconditionally (it's small) but only LINKED into builds where one of the two flags is set, so mode-0 doesn't pay the footprint cost.

Track 1 (host Python) and Track 3 (flashable ZIP) are external to the .efi build entirely — no payload size impact.

### Size budget

Currently:
- mode-0.efi: 524K
- mode-1.efi: 548K
- mode-1-auto-debug-verbose.efi: 561K

Plan 3a + 3d additions (mode-1+):
- AvbParseLib: ~5K
- Track 2 fastboot oem: ~15K
- Plan 3d getvar + oem: ~12K

Estimated mode-1.efi after this work: ~580-590K. Mode-0 unchanged.

### Size monitoring

To stay under control:
- Document EFISP partition size in the spec once known. Runbook for the user: `fastboot reboot bootloader; fastboot getvar partition-size:efisp; fastboot getvar partition-size:efisp_a; fastboot getvar partition-size:efisp_b`.
- Compare against gbl_root_canoe's 208K baseline. We're carrying more (LogFsLib, ProtocolHookLib, FastbootLib customizations) but should profile breakdown during implementation.
- If post-Plan-3a/3d size exceeds (say) 600K, profile via `objdump -h` and trim:
  - Drop unused fastboot commands cherry-picked but never invoked.
  - Lazy-init AvbParseLib only when graft/status fastboot command is invoked (saves DXE-init cost).
  - Strip debug strings if size is critical.
- Add a `tests/060_size_budget_lint.sh` that warns if any artifact in `dist/` exceeds 600K. Hard fail at 700K.

## Section 6 — Conclusion: why grafting just the vbmeta footer satisfies AOSP

The user observed: it's "funny" that grafting just the embedded vbmeta footer (and its associated header + auxiliary data block) is enough to satisfy AOSP — the OS doesn't recheck the actual recovery content. That's not a bug or coincidence; it's a fundamental property of AVB's chain architecture.

### How AVB chain validation actually works

Main `vbmeta.img` contains a `chain_partition_descriptor` for each chained partition (recovery, dtbo, vbmeta_system, vbmeta_vendor on this device). The chain descriptor stores:

- `partition_name` (ASCII)
- `rollback_index_location`
- `public_key_size` + the public key bytes (or a hash thereof)
- flags

It does **not** store any hash of the chained partition's content.

When libavb walks the descriptors during `avb_slot_verify_full()`:

1. For each `chain_partition_descriptor`, libavb invokes `libavb_ops::read_from_partition()` to read the chained partition's last 64 bytes — the `AvbFooter`.
2. It parses the footer to find the embedded vbmeta region (offset = `partition_size - footer.vbmeta_offset - footer.vbmeta_size`), reads that region.
3. Parses the embedded `AvbVBMetaImageHeader` and verifies its signature against the chain descriptor's stored public key.
4. The signature in step 3 covers the header struct + the auxiliary data block (which contains inner descriptors). It does NOT cover the partition content above the vbmeta region.
5. After signature passes, libavb walks the inner descriptors recursively — for things like dm-verity (`hashtree_descriptor`) or content hash (`hash_descriptor`).

Step 4 is the architectural invariant. The signature is over a fixed-size metadata blob, not over the partition payload.

### Why the inner descriptors don't catch custom content

For the recovery partition specifically, the inner descriptors typically include a `hashtree_descriptor` (dm-verity setup info) or a `hash_descriptor` over the recovery image content.

Both are checked at *use time*, not at libavb verification time:

- **hashtree_descriptor**: dm-verity is set up by the kernel when the partition is mounted as a dm-verity volume. The hashes are checked block-by-block on read. Recovery is **not mounted** as a dm-verity volume; it's loaded as a kernel image into RAM via the boot.img mechanism when `oplusboot.mode=recovery`. dm-verity never gets set up for recovery → inner hashtree claims are never enforced.
- **hash_descriptor**: a one-shot hash check. For partitions that ARE checked at boot time (e.g. boot, init_boot), libavb computes the hash over the partition content and compares to the descriptor's stored hash. For partitions like recovery that are NOT in the boot path during normal boot, the hash check is conditional — first_stage_init's `AvbHandle::Open()` calls `avb_slot_verify` which DOES walk descriptors, but the result is used to gate dm-verity setup, not to abort if a non-mounted partition's hash mismatches.

So the chain check (step 4) is the only enforcement gate that fires regardless of whether the partition is later mounted. That gate checks the SIGNATURE on the metadata blob — and the signature is valid because the metadata blob is bytewise stock.

### Implication

Grafting just the stock embedded vbmeta region into custom recovery passes the chain check. The custom kernel/ramdisk content above the vbmeta region is never re-hashed against the descriptor's claims because:

- dm-verity isn't applied to recovery.
- The signed metadata says "recovery should look like X" but no part of AOSP's boot path actually verifies recovery's content matches X (the descriptor is informational for recovery, enforced only for partitions that participate in dm-verity).

This is what gbl_root_canoe's author exploits, and what we're now exploiting too. The graft is architecturally sound, not a clever hack — it's an intended property of AVB's separation between metadata signing (boot-time, by libavb) and content verification (use-time, by dm-verity or image-load mechanisms).

### Why we wouldn't worry about this changing

- AVB's chain architecture has been stable since AVB 2.0 (2018).
- Changing it would break every Android device that uses chain partitions.
- AOSP would need to add explicit "verify chained partition content at boot time" code, which would dramatically slow boot (full SHA256 over each chained partition).
- There's no security incentive for this change: the chain signature already proves a stock vbmeta header was present at flash time; whether the content above it has been replaced is the user's choice on their unlocked device.

The graft works today. It will keep working through future AVB versions unless the architecture fundamentally changes.

## Section 7 — Stop-lines

- Do not re-introduce `oem pull-logfs` or any of the other dead-end fastboot commands removed in Plan 1.
- Do not link full libavb into our payload. Use `AvbParseLib` (custom parser) instead.
- Do not cache stock vbmeta in our payload's FV (would be stale across OTAs and would bloat).
- Do not write to a partition without dry-run + commit confirmation in Track 2.
- Do not enable `GBL_AVB_GRAFT_FASTBOOT` or `GBL_AVB_STATUS` in mode-0 (mode-0 stays minimal).
- Do not include `--disable-verification` flag manipulation tooling in this spec. That's user's responsibility (Magisk/KSUN style installers handle it).

## Section 8 — Out of scope

- Mode-2 KM-payload spoof (Play Integrity / SafetyNet) — separate plan.
- ABL caching / build-time embed — separate plan (Plan 3b in the broader roadmap).
- Hash-descriptor partition coverage — handled by user's existing vbmeta flag setup.
- Init_boot patcher tooling — handled by KSUN/AK3.
- vbmeta_system / vbmeta_vendor coverage — OEM-signed, rarely user-replaced.
- A persistent vbmeta cache — on-disk stock IS the cache, refreshed naturally by OTAs.

## Section 9 — Implementation outline (for writing-plans phase)

Sequence sketch:

1. **AvbParseLib** — implement footer/header/descriptor parsing with TDD. Synthetic AVB data fixtures. ~10 host-side test cases.
2. **Track 1 host script** (`scripts/graft-vbmeta.py`) — graft logic in pure Python. Test against known-good infiniti EU 16.0.5.703 stock recovery: extract → graft into a synthetic custom payload → verify the grafted output's vbmeta region is bytewise equal to stock's vbmeta region.
3. **Track 2 on-device fastboot oem** — add `oem graft-and-flash` to FastbootCmds (in edk2 fork), wire in AvbParseLib, add dry-run + commit semantics. Stretch: `--backup` flag.
4. **Plan 3d getvar + oem vbmeta-status** — add fastboot getvar handlers + multi-line oem command. Wire SHA256 from BaseCryptLib (or a small standalone).
5. **Build flag plumbing** — `GBL_AVB_GRAFT_FASTBOOT` and `GBL_AVB_STATUS` in DSC + DEC. Default ON for mode-1+, OFF for mode-0.
6. **Track 3 flashable ZIP** — `update-binary` script, ports the Python graft logic to a small static-linked C/Go binary suitable for recovery's busybox env. ZIP template.
7. **Size budget lint** — `tests/060_size_budget_lint.sh` (warn at 600K, fail at 700K).
8. **Update docs/re/avb-input-facade.md** — mark Option E (graft) as implemented; cross-link to this spec.
9. **Validation** — end-to-end on infiniti EU 16.0.5.703: graft custom recovery via Track 1 → flash → mode-1 + grafted recovery → boot system. Verify `ro.boot.vbmeta.recovery.digest` matches stock (or is plausible) and userspace `AvbHandle` validation passes.

End-state checklist (for verifier):

- [ ] `scripts/graft-vbmeta.py` produces grafted output that passes a host-side AvbParseLib parse.
- [ ] `fastboot oem graft-and-flash recovery` dry-run prints sane diagnostics.
- [ ] `fastboot oem graft-and-flash recovery commit` writes successfully and post-write `getvar vbmeta:recovery:status` returns `ok`.
- [ ] `fastboot oem vbmeta-status` returns multi-line report with all known partitions enumerated.
- [ ] After grafting custom recovery + reboot to system: device boots Android system with custom recovery still flashed (per the central goal).
- [ ] Mode-0.efi unchanged size (524K) — no Plan 3a/3d code linked.
- [ ] Mode-1.efi size within budget (target <600K).
- [ ] Track 3 flashable ZIP roundtrips: install in custom recovery, grafts, flashes, reboot to system works.

## Section 10 — Open items deferred to writing-plans

- Specific edk2-side INF/DSC paths for the new fastboot commands (depends on FastbootCmds.c structure).
- Decision on SHA256 source: BaseCryptLib (heavy) vs standalone (light). Profile in plan-writing.
- Choice of static-linked binary toolchain for Track 3 (Go or C; Go simpler but bigger; C requires aarch64-linux-gnu cross-compile).
- Specific `update-binary` shell dialect compatibility (TWRP vs OFOX vs LineageOS recovery — script needs to be busybox-portable).
- Test fixture set for AvbParseLib: synthetic AVB data construction + at least one real partition (extracted from infiniti EU 16.0.5.703 stock recovery).
- EFISP partition size on infiniti — fill in once we have `fastboot getvar` evidence.
