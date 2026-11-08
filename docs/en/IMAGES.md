# Image packaging

The trio: `boot` + `system_dlkm` + `vendor_dlkm` **must come from the same source
and the same profile**, otherwise the `module_layout` CRC will not match and all
modules will fail to load.

## 1. Artifacts overview

| Image | Size | Contents | Packaging |
|---|---|---|---|
| `boot.img` | 17.2 MB | kernel `Image.lz4` (18.3 MB), header v4, no ramdisk | `mkbootimg.py` |
| `system_dlkm.img` | 6.3 MB | 58 GKI modules (from the `kernel/` staging dir) | `make_system_dlkm.sh` |
| `vendor_dlkm.img` | 19.7 MB | 271 vendor modules (from `extra/`) | `make_vendor_dlkm_trim.sh` |

The stock images are 15.4 MB / 7.3 MB (stock `vendor_dlkm` contains only 206
modules). This project's images are slightly larger because the module set is
more complete and the build options differ (the average module size is about
1.6 times the stock one).

## 2. Partition contract (three hard constraints)

All three come from on-device debugging; violating any one of them leaves the
device stuck at the logo:

### 2.1 Flat layout

```
/vendor_dlkm/lib/modules/
├── *.ko                     ← modules live directly at this level
├── modules.load             ← init reads this path directly
├── modules.dep              ← contents use absolute paths /vendor_dlkm/lib/modules/x.ko
└── modules.alias / modules.softdep / modules.order
```

If packaged as `lib/modules/<release>/` (the default layout of Kleaf
`kernel_modules_install`), even with correct labels, init cannot find
`modules.load` and **not a single module will be loaded**.

### 2.2 SELinux labels and permissions

`mkfs.erofs` must be invoked with:

```bash
mkfs.erofs -zlz4hc -T 0 --all-root \
  --mount-point=/vendor_dlkm \
  --fs-config-file=<per-path permission table> \
  --file-contexts=<per-path SELinux contexts> \
  vendor_dlkm.img <staging_dir>
```

Missing the last three options makes all files `u:object_r:unlabeled:s0`,
`init.insmod.sh` is denied by `avc: denied`, and modules fail to load.

The contexts and permission table are generated **per path** by the scripts
from the actual staged contents; the stock static list cannot be reused — the
stock list enumerates files under the original kernel release directory name
(`…gfdf3de8ccf1b`), so any sublevel change breaks the match.

### 2.3 Module signing

At packaging time, `tools/sign_modules.sh` runs `sign-file sha1` on every `.ko`
using the same `certs/signing_key.pem/x509` used to build the kernel.

The reason is explained in item 4 of [PITFALLS.md](PITFALLS.md): an unsigned
module that exports a GKI protected symbol is rejected.

Verification:

```bash
strings -a xxx.ko | grep 'Module signature appended'   # one hit means signed
```

## 3. Load-list maintenance

### 3.1 Two lists with different purposes

| File | Purpose | Who reads it |
|---|---|---|
| `tools/vendor_dlkm/image_contents.load` | **Image contents**: which `.ko` files are packed into the image | Packaging scripts |
| `tools/vendor_dlkm/modules.load.stock` | Stock boot load order (187 entries, baseline, do not modify) | Packaging scripts |
| `tools/vendor_dlkm/modules.load.extra` | Additional modules inserted into the stock order (5 entries) | Packaging scripts |
| `vendor_dlkm_modules.load` (repository root) | Input to Kleaf's built-in `kernel_images` target, **full-path** format | `BUILD.bazel` |

The file at the repository root is for Kleaf's `mgk_64_k61_images`, so its
format must use full paths: Kleaf internally runs `grep -w -f list modules.order`,
and basenames may falsely match because `-` is not a word character (`vcp.ko`
matches `mtk-mmdebug-vcp.ko`). Its artifact is uncompressed and large;
**it is not used for flashing** and is kept only to keep the `kernel_images`
target usable.

The packaging scripts merge `stock` and `extra` into the image's `modules.load`
(192 entries); **the order matches the stock one line by line, with the only
difference being the 5 inserted lines**.

### 3.2 Load mechanism

`init.insmod.sh` runs during `early-init`:

```sh
modprobe -a -d /vendor/lib/modules $(cat modules.load)
```

- Dependencies are resolved through `modules.dep`; if the provider is present
  in the image it is brought up automatically, **so there is no need to list
  it**;
- Exception: `zram_ext.ko` / `extend_reclaim.ko` are `insmod`ed separately by
  the script (bypassing modprobe), so `zsmalloc.ko` must be explicitly listed in
  `modules.load.extra`.

### 3.3 Deciding whether to add an entry

| Scenario | Action |
|---|---|
| Shipped with the image and `insmod`ed on demand by `.rc` | Add only to `image_contents.load` |
| Must be loaded automatically at boot | Also add to `modules.load.extra` |
| Provides symbols for other modules | No need to list it; `modprobe` brings it up via `modules.dep` |

## 4. boot image

```bash
python3 tools/mkbootimg/mkbootimg.py --header_version 4 \
  --kernel out/kernel_modules/Image.lz4 \
  --output out/kernel_modules/boot.img
```

Key points:

- **The kernel must be LZ4-compressed**. The first 4 bytes of the kernel
  section in the stock boot image are `02 21 4c 18`. If the uncompressed `Image`
  (38 MB) is used by mistake, the bootloader fails to decompress it and stops
  immediately — even fastbootd cannot be reached.
- Header v4, no ramdisk, no cmdline, matching the stock boot header.

## 5. Flashing and rollback

```bash
adb reboot bootloader
fastboot reboot fastboot                      # dynamic partitions require fastbootd
fastboot flash boot         boot.img
fastboot flash system_dlkm  system_dlkm.img
fastboot flash vendor_dlkm  vendor_dlkm.img
fastboot reboot
```

A change in image size automatically triggers a logical partition resize
(`Resizing 'vendor_dlkm_a'`). Before each flash, back up the currently bootable
three images locally as a rollback baseline.

## 6. Artifact list

`out/kernel_modules/` (output of `mgk_64_k61_dist`, flat):

| File | Description |
|---|---|
| `Image` / `Image.gz` / `Image.lz4` | Kernel images (38 MB / 18.3 MB) |
| `System.map` / `vmlinux` / `vmlinux.symvers` | Debug and symbol information |
| `*.ko` (720) | All built modules (stripped) |
| `vendor_dlkm.img` | Kleaf-built full image (uncompressed, large, not recommended for flashing) |
