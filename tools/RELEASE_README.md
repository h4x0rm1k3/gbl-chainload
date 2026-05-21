# gbl-chainload host tools

Release bundle layout:

```text
efisp-package.py
vbmeta-graft.py
bin/<tool>
SHA256SUMS
VERSION
```

`efisp-package.py` and `vbmeta-graft.py` auto-discover tools from `./bin`.
Individual tools can be run as `./bin/<tool>`.

## Quick checks

```bash
sha256sum -c SHA256SUMS
python3 efisp-package.py --version
for t in fv-unwrap abl-patcher gbl-pack gbl-commit vbmeta-graft mode2-profile gblp1-inspect; do ./bin/$t --version; done
```

If `sha256sum` reports `FAILED`, do not use the bundle.

## Package an EFISP image

Required files:

| Mode | Base EFI | Other inputs |
|---|---|---|
| 0 | `mode-0.efi` | stock ABL dump |
| 1 | `mode-1.efi` | stock ABL dump |
| 2 | `mode-2.efi` | stock ABL dump, stock main `vbmeta.img`, OEM id |

Example mode 1:

```bash
python3 efisp-package.py \
  --abl  stock_abl.img \
  --mode 1 \
  --efi  mode-1.efi \
  --out  installed-mode1.efi
```

Example mode 2:

```bash
python3 efisp-package.py \
  --abl          stock_abl.img \
  --mode         2 \
  --efi          mode-2.efi \
  --stock-vbmeta stock_vbmeta.img \
  --oem          oneplus \
  --out          installed-mode2.efi
```

Before booting, inspect the appended GBLP1 payload:

```bash
./bin/gblp1-inspect installed-mode1.efi
```

Device testing is RAM-only:

```bash
fastboot stage installed-mode1.efi
fastboot oem boot-efi
```

Do not flash firmware partitions while iterating on host-tool output.

## Graft a partition image

`vbmeta-graft.py` is the convenience path for partition images such as
`system`, `vendor`, `product`, `dtbo`, or other AVB-chained partitions. It
defaults the final partition size to `custom_partition.img`'s file size, which
is normally what you want when that file is the full image you intend to write.
Use `--part-size` or `--size-from` when the custom image is trimmed/bare and the
destination partition size differs.

```bash
python3 vbmeta-graft.py \
  --stock  stock_partition.img \
  --custom custom_partition.img \
  --out    grafted_partition.img
```

The raw tool is still available for inspection and fully manual control:

```bash
./bin/vbmeta-graft list stock_partition.img
./bin/vbmeta-graft list-hash stock_partition.img
./bin/vbmeta-graft graft \
  --stock     stock_partition.img \
  --custom    custom_partition.img \
  --part-size <target-partition-bytes> \
  --out       grafted_partition.img

./bin/vbmeta-graft check grafted_partition.img vbmeta.img <partition-name>
```

`graft` does not need the main `vbmeta.img`; `check` does.

## Tool map

| Tool | Purpose |
|---|---|
| `fv-unwrap` | Extract PE32 payload from an ABL/FV image |
| `abl-patcher` | Apply or check mode-specific ABL patches |
| `gbl-pack` | Build GBLP1 overlays |
| `gbl-commit` | Write/verify files or block devices intentionally |
| `vbmeta-graft` | Build grafted AVB partition images |
| `vbmeta-graft.py` | Convenience wrapper that infers graft partition size |
| `mode2-profile` | Derive/compile mode-2 profile data |
| `gblp1-inspect` | Verify GBLP1 entries and SHA-256 status |
