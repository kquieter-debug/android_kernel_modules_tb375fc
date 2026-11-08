# MT6897 Kernel Modules (TB375FC)

**English** | [简体中文](README.md)

Vendor kernel module project for the MT8792 / MT6897 tablet
(`ro.product.device=TB375FC`), built with Kleaf. It mainly produces
`vendor_dlkm`, i.e. the vendor-side kernel modules.

For a full build, it must be used together with the
[common](https://github.com/kquieter-debug/Android_kernel_source_TB375FC) kernel
source that provides the Bazel/kleaf build platform. The kernel and the modules
**must come from the same profile**; otherwise the `module_layout` CRC will not
match, `insmod` will return `Exec format error`, and the device will fail to
boot normally, causing a series of related problems.

## Current status

| Item | Value |
|---|---|
| Kernel | 6.1.138-android14-11 (`mgk_64_k61` profile) |
| `module_layout` CRC | `0xea759d7f` (matches stock GKI and the 222 modules in `vendor_boot`) |
| Modules built | 720 |
| `vendor_dlkm` image content | 271 modules |
| Boot-time load list | 192 entries, **192/192 load successfully on hardware** |
| Total modules on device | 404 |
| Verified features | display, touch, GPU, WiFi, BT, audio, video encode/decode, camera, sensors, ZRAM |

## Quick start

Run the following commands from the Kleaf workspace root (the directory
containing `WORKSPACE`, `common/`, and `common-modules/`):

```bash
# 1) Build the kernel and all modules (about 25 minutes on a cold cache)
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install

# 2) Distribute to <workspace>/out/kernel_modules
tools/bazel run //common-modules/mediatek:mgk_64_k61_dist

# 3) Package the two partition images (modules are signed inside)
bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh
bash common-modules/mediatek/tools/make_system_dlkm.sh

# 4) Package boot (header v4, no ramdisk, kernel must be LZ4-compressed)
python3 tools/mkbootimg/mkbootimg.py --header_version 4 \
  --kernel out/kernel_modules/Image.lz4 --output out/kernel_modules/boot.img
```

> `mgk_64_k61` is a `kernel_build` (analysis target) and **cannot be `run`**;
> the only runnable target is `mgk_64_k61_dist`.

## Repository layout

```
common-modules/mediatek/
├── BUILD.bazel                      Kleaf targets: kernel / modules / images / dist
├── mgk_ko.bzl                       define_mgk_ko(): Kleaf wrapper for a single module
├── build.config.mgk.aarch64         vendor profile build config
├── kernel_device_modules-6.1/       MTK device drivers (BSP delivery)
├── vendor/mediatek/kernel_modules/  standalone module set (connectivity / gpu / mtkcam / …)
├── tools/
│   ├── make_vendor_dlkm_trim.sh     vendor_dlkm image packaging
│   ├── make_system_dlkm.sh          system_dlkm image packaging
│   ├── sign_modules.sh              module signing (called by the two scripts above)
│   ├── solve_deps.py                offline symbol-dependency diagnostics (not for load lists)
│   └── vendor_dlkm/ system_dlkm/    load lists, SELinux contexts, fs_config
└── docs/
    ├── zh/                          Chinese documentation
    └── en/                          English documentation
```

## Documentation

| Document | Contents |
|---|---|
| [BUILD.md](docs/en/BUILD.md) | Workspace layout, toolchain versions, build targets, config sources, adding a module |
| [IMAGES.md](docs/en/IMAGES.md) | Packaging details for both dlkm images and boot, load-list maintenance, signing |
| [PITFALLS.md](docs/en/PITFALLS.md) | **Pitfalls**: SELinux labels, flat layout, load order, dependency poisoning, signing |
| [NOTICE_en.md](NOTICE_en.md) | Module provenance, upstream commits, licensing |

## Three hard constraints

All three were verified on hardware; violating any one of them leaves the device
stuck at the boot logo:

1. **Modules must be laid out flat**: `lib/modules/*.ko` must be in the same
   directory as `modules.load` and `modules.dep`, and `modules.dep` must use
   absolute paths (`/vendor_dlkm/lib/modules/xxx.ko`).
2. **`modules.load` must follow the stock order**: the order encodes the
   hardware power-up sequence and must not be reordered by symbol dependencies.
3. **Modules must be signed**: an unsigned module that exports a GKI protected
   symbol is rejected by the kernel (`EACCES`).

See [docs/en/PITFALLS.md](docs/en/PITFALLS.md) for details and diagnostics.

## License

The parts owned by this repository (`tools/`, `docs/`, build files) are licensed
under [GPL-2.0](LICENSE), as declared by the [LICENSE](LICENSE) file at the
repository root. Aggregated third-party components (MTK vendor drivers, upstream
modules) keep the terms declared in their own directories; they are a mix of
GPL / Dual BSD-GPL / Proprietary, and GPL-2.0 does not cover them.

See [NOTICE_en.md](NOTICE_en.md) for provenance and upstream commit IDs.
