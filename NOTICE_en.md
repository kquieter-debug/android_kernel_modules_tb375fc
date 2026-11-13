# Provenance and License Notice

This repository is an aggregation project for vendor kernel modules for the
MT6897 (TB375FC) device, used for Kleaf builds and partition image packaging.
The code comes from multiple parties; **the license declared in each directory
prevails**, and this file only records provenance.

## 1. Sources

| Source | Version / Commit | Coverage |
|---|---|---|
| [Moto Pad 60 Pro BSP delivery](https://github.com/MotorolaMobilityLLC/MotoTablets/releases) | Linux 6.1.138 | Original main body of `kernel_device_modules-6.1/` and `vendor/` |
| [mt6897-devs/android_vendor_mediatek_kernel_modules](https://github.com/mt6897-devs/android_vendor_mediatek_kernel_modules) | `lineage-23.2` @ `6bceb1b8e3444dd6caf80ad7470c9e57bbdcd1ab` | `connectivity/wlan/core/gen4m`, `hbt_driver`, `hbt_driver_cus` |
| [MiCode/MTK_kernel_modules](https://github.com/MiCode/MTK_kernel_modules) | `bsp-duchamp-u-oss` @ `e57cdab505850464e0179c8cb16d17b09cf8c524` | `fpsgo_cus` |

Reasons for including them:

- **wlan gen4m**: the BSP package contains a dangling symlink for
  `connectivity/wlan/core/gen4m`, and the actual driver is missing. This
  directory is the only source of `wlan_drv_gen4m_6897.ko` (CONNAC2X2_SOC7_0),
  so it was completed from the `lineage-23.2` branch for the same SoC.
- **hbt_driver**: the BSP package does not contain this directory. `hbt.ko` is a
  GPL misc device plus an `mmap_lock` tracking driver.
- **fpsgo_cus**: the BSP package only contains `performance/fpsgo_v3` (which
  produces `mtk_fpsgo.ko`); `fpsgo.ko` is its customer-specific layer
  (`depends=mtk_fpsgo`).
- **hbt_driver_cus**: it produces the same `hbt.ko` as `hbt_driver`, and the two
  are mutually exclusive. Currently `hbt_driver` is integrated; `hbt_driver_cus`
  keeps its source only and does not participate in the build.

## 2. Licensing

The `LICENSE` file at the repository root is **GPL-2.0**, and applies to the
parts owned by this repository (`tools/`, `docs/`, build files). The aggregated
third-party components are licensed according to their own declarations:

| Scope | License |
|---|---|
| Parts owned by this repository (`tools/`, `docs/`, build files) | GPL-2.0 (see `LICENSE`) |
| MTK vendor driver main body (`kernel_device_modules-6.1/`, `vendor/`) | Per-file header declarations; a mix of GPL-2.0 and Proprietary |
| `hbt_driver`, `hbt_driver_cus` | GPL-2.0 |
| `connectivity/wlan/core/gen4m` | Dual BSD / GPL |
| `fpsgo_cus` | Proprietary (customer-specific layer) |

**Note**: the repository contains Proprietary components, which GPL-2.0 cannot
cover. Before distributing or redistributing, verify the terms of each
component yourself; `LICENSE` only grants rights to the parts owned by this
repository. This repository is for maintenance and debugging of the owner's
own device.

## 3. Prebuilt objects

Some modules ship prebuilt `.o` files with the source (`.o_shipped`,
`.cmd_shipped`) to avoid rebuilding closed-source parts. `.gitignore` explicitly
allows these files; other build artifacts are not committed.

## 4. Third-party code

`kernel_device_modules-6.1/` and `vendor/` contain code derived from the Linux
kernel. Copyright belongs to their respective authors and is governed by the
corresponding GPL-2.0 terms. This repository claims no rights over that code.
