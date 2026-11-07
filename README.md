# MT6897 Kernel Modules (TB375FC)

**English** | [简体中文](README.zh-CN.md)

Vendor kernel modules for the MT8792 / MT6897 tablet (`ro.product.device=TB375FC`),
built with Kleaf. Produces the `.ko` set and partition images for `vendor_dlkm`
and `system_dlkm`.

Used together with `TB375FC/common` (Linux 6.1.138 + MTK merged kernel). The
kernel and the modules **must come from the same build profile** — otherwise the
`module_layout` CRC differs and `insmod` fails with `Exec format error`.

## Status

| Item | Value |
|---|---|
| Kernel | 6.1.138-android14-11 (`mgk_64_k61` profile) |
| `module_layout` CRC | `0xea759d7f` (matches stock GKI and the 222 modules in `vendor_boot`) |
| Modules built | 720 |
| `vendor_dlkm` image content | 271 modules |
| Boot-time load list | 192 entries — **192/192 load successfully on hardware** |
| Modules loaded on device | 404 |
| Verified working | display, touch, GPU, WiFi, BT, audio, video codecs, camera, sensors, ZRAM |

## Quick start

Run from the Kleaf workspace root (the directory containing `WORKSPACE`,
`common/` and `common-modules/`):

```bash
# 1) Build the kernel and all modules (about 25 min on a cold cache)
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install

# 2) Distribute to <workspace>/out/kernel_modules
tools/bazel run //common-modules/mediatek:mgk_64_k61_dist

# 3) Build the two dlkm partition images (modules are signed here)
bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh
bash common-modules/mediatek/tools/make_system_dlkm.sh

# 4) Build boot (header v4, no ramdisk, kernel must be LZ4-compressed)
python3 tools/mkbootimg/mkbootimg.py --header_version 4 \
  --kernel out/kernel_modules/Image.lz4 --output out/kernel_modules/boot.img
```

> `mgk_64_k61` is a `kernel_build` (analysis target) and **cannot be `run`**.
> The only runnable target is `mgk_64_k61_dist`.

## Repository layout

```
common-modules/mediatek/
├── BUILD.bazel                      Kleaf targets: kernel / modules / images / dist
├── mgk_ko.bzl                       define_mgk_ko(), the per-module wrapper
├── build.config.mgk.aarch64         vendor profile build config
├── kernel_device_modules-6.1/       MTK device drivers (BSP delivery)
├── vendor/mediatek/kernel_modules/  standalone module set (connectivity / gpu / mtkcam / …)
├── mipp/                            stylus (MIPP) shim module
├── tools/
│   ├── make_vendor_dlkm_trim.sh     vendor_dlkm image builder
│   ├── make_system_dlkm.sh          system_dlkm image builder
│   ├── sign_modules.sh              module signing (called by both builders)
│   ├── solve_deps.py                offline symbol-dependency diagnostics (not for load lists)
│   └── vendor_dlkm/ system_dlkm/    load lists, SELinux contexts, fs_config
└── docs/                            detailed documentation
```

## Documentation

| Document | Contents |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | Workspace layout, toolchain versions, build targets, config sources, adding a module |
| [docs/IMAGES.md](docs/IMAGES.md) | Packaging of both dlkm images and boot, load-list maintenance, module signing |
| [docs/PITFALLS.md](docs/PITFALLS.md) | **Pitfalls**: SELinux labels, flat layout, load order, dependency poisoning, signing |
| [NOTICE.md](NOTICE.md) | Module provenance, upstream commits, licensing |

> The `docs/` pages are currently written in Chinese. English translations can be
> added on request.

## Three hard constraints

All three were established on hardware; violating any of them leaves the device
stuck on the boot logo:

1. **Modules must be laid out flat**: `lib/modules/*.ko` together with
   `modules.load` and `modules.dep` in the same directory, and `modules.dep` must
   use absolute paths (`/vendor_dlkm/lib/modules/x.ko`).
2. **`modules.load` must keep the stock order**: the order encodes the hardware
   power-up sequence and cannot be replaced by a symbol-topology sort.
3. **Modules must be signed**: an unsigned module that exports a GKI protected
   symbol is rejected by the kernel with `EACCES`.

See [docs/PITFALLS.md](docs/PITFALLS.md) for details and diagnostics.

## License

The parts this repository owns (`tools/`, `docs/`, `mipp/`, build files) are
licensed under [GPL-2.0](LICENSE). Vendored third-party components (MTK vendor
drivers, upstream modules) keep the terms declared in their own directories, a
mix of GPL / Dual BSD-GPL / Proprietary — GPL-2.0 does not cover those.

Provenance and upstream commit IDs: [NOTICE.md](NOTICE.md).
