# Build

## 1. Workspace layout

This directory must be inside a Kleaf workspace to build. The workspace must
contain at least:

```
<workspace>/
├── WORKSPACE -> build/kernel/kleaf/bazel.WORKSPACE
├── build/kernel/                     kleaf (bazel.sh, bazel.WORKSPACE, build rules)
├── build/bazel_common_rules/         copy_to_dist_dir and others
├── common/                           kernel source (6.1.138)
├── common-modules/mediatek/          this repository
└── prebuilts/{clang,build-tools,kernel-build-tools,jdk,bazel,…}
```

The official way is to fetch the AOSP Kleaf workspace with `repo`, then replace
`common` and `common-modules`:

```bash
repo init -u https://android.googlesource.com/kernel/manifest \
  -b common-android14-6.1-2025-07
repo sync -j8
```

Manifest baseline for this workspace: `kernel/superproject` =
`common-android14-6.1-2025-07`, `build/kernel` = `main-kernel-build-2023`.

## 2. Toolchain versions

| Component | Version | Location |
|---|---|---|
| Clang / LLD | `clang-r487747c` (17.0.2) | `prebuilts/clang/host/linux-x86/` |
| Bazel | wrapped by `tools/bazel` | `prebuilts/bazel/linux-x86_64/` |
| Image tools | `mkfs.erofs`, `depmod`, `avbtool` | `prebuilts/kernel-build-tools/linux-x86/bin/` |
| JDK | 11 | `prebuilts/jdk/jdk11/` |
| Signing tool | `scripts/sign-file` (built locally with gcc) | `tools/bin/` (gitignored) |

`tools/bazel` is a symlink to `build/kernel/kleaf/bazel.sh`, which sets
`--output_user_root=out/bazel/output_user_root`. **Do not invoke the system
bazel directly.**

## 3. Build targets

| Target | Purpose |
|---|---|
| `//common-modules/mediatek:mgk_64_k61_kernel_aarch64` | Kernel (notrim, exports non-KMI symbols) |
| `//common-modules/mediatek:mgk_64_k61` | kernel_build for module preparation (`make modules`) |
| `//common-modules/mediatek:mgk_64_k61_modules` | kernel_module_group for all modules |
| `//common-modules/mediatek:mgk_64_k61_modules_install` | Install into the staging directory |
| `//common-modules/mediatek:mgk_64_k61_images` | Kleaf-built `vendor_dlkm.img` (compression disabled, larger) |
| `//common-modules/mediatek:mgk_64_k61_dist` | Distribute to `out/kernel_modules` (the only target that can be `run`) |

## 4. Configuration sources

`build.config.mgk.aarch64` defines `KERNEL_DIR=common` and merges three configs
in order in `PRE_DEFCONFIG_CMDS`:

```
gki_defconfig
  + kernel_device_modules-6.1/arch/arm64/configs/mgk_64_k61_defconfig
  + kernel_device_modules-6.1/kernel/configs/mgk_debug_min.config
```

### Why the third file is not `userdebug.config`

The original BSP uses `kernel/configs/userdebug.config`, but that file changes
the layout of `struct module`, making the kernel's `module_layout` become
`0x9efa483d`, while the stock kernel, stock `system_dlkm`, and the 222 modules
in `vendor_boot` expect `0xea759d7f`.

As a result, any module `insmod` reports:

```
disagrees about version of symbol module_layout   (userspace sees Exec format error)
```

`mgk_debug_min.config` keeps only the four switches that produce `.ko` files
(`MTK_IRQ_MONITOR`, `MTK_SELINUX_AEE_WARNING`, `MEDIATEK_CPUFREQ_DEBUG`,
`PSTORE_BLK`) plus the pstore parameters. After the replacement, the CRC
exactly matches the stock one.

> Conclusion: **before adding a `CONFIG_*`, check whether it affects
> `struct module` or the KMI CRC**; if it does, the baseline must be re-aligned.

Add `CONFIG_*` options required by new modules to `mgk_64_k61_defconfig`; symbols
absent on the kernel side are supplemented via
`kernel_device_modules-6.1/Kconfig.ext`.

## 5. Adding a module

1) Put the source under `vendor/mediatek/kernel_modules/<name>/`, and provide a
   `Makefile` (recursive driver) and `Kbuild` (`obj-m` definition) in that
   directory:

```make
# Makefile: Kleaf invokes it with make -C <module_dir>; all/modules_install/clean are required
all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
```

2) Add `BUILD.bazel`:

```python
load("//common-modules/mediatek:mgk_ko.bzl", "define_mgk_ko")

define_mgk_ko(
    name = "foo",
    outs = ["foo.ko"],
    # deps = ["//…:conninfra"],   # when Module.symvers from other modules is needed
)
```

3) Add the target to `MGK_64_K61_KLEAF_MODULES` in
   `kernel_device_modules-6.1/kleaf_modules.bzl` (sorted by path).

4) If the module must be loaded at boot, add an entry to
   `tools/vendor_dlkm/modules.load.extra`; if it is only shipped with the image
   and loaded on demand by `.rc`, add it only to `image_contents.load`. See
   [IMAGES.md](IMAGES.md) for the difference.

## 6. Device tree

The board DTS is located at
`kernel_device_modules-6.1/arch/arm64/boot/dts/mediatek/tb8792p1_64_wifi/` and is
compiled together by the `mgk_64_k61` kernel_build.
