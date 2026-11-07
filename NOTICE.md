# 来源与许可声明

本仓库是 MT6897（TB375FC）设备的厂商内核模块聚合工程，用于 Kleaf 构建与
分区镜像打包。代码来自多方，**许可证以各目录内的声明为准**，本文件只做溯源记录。

## 1. 来源

| 来源 | 版本 / 提交 | 覆盖范围 |
|---|---|---|
| 设备厂商 BSP 交付包 | Linux 6.1.138 | `kernel_device_modules-6.1/` 与 `vendor/` 的原始主体 |
| [mt6897-devs/android_vendor_mediatek_kernel_modules](https://github.com/mt6897-devs/android_vendor_mediatek_kernel_modules) | `lineage-23.2` @ `6bceb1b8e3444dd6caf80ad7470c9e57bbdcd1ab` | `connectivity/wlan/core/gen4m`、`hbt_driver`、`hbt_driver_cus` |
| [MiCode/MTK_kernel_modules](https://github.com/MiCode/MTK_kernel_modules) | `bsp-duchamp-u-oss` @ `e57cdab505850464e0179c8cb16d17b09cf8c524` | `fpsgo_cus` |

引入原因：

- **wlan gen4m**：BSP 包内 `connectivity/wlan/core/gen4m` 为悬空软链接，实际驱动缺失。
  该目录是 `wlan_drv_gen4m_6897.ko`（CONNAC2X2_SOC7_0）的唯一来源，从同 SoC 的
  lineage-23.2 分支补齐。
- **hbt_driver**：BSP 包内无此目录。`hbt.ko` 是 GPL 的 misc 设备 + `mmap_lock`
  跟踪驱动。
- **fpsgo_cus**：BSP 包内只有 `performance/fpsgo_v3`（产出 `mtk_fpsgo.ko`），
  `fpsgo.ko` 是其客户定制层（`depends=mtk_fpsgo`）。
- **hbt_driver_cus**：与 `hbt_driver` 同样产出 `hbt.ko`，两者互斥。当前接入
  `hbt_driver`，`hbt_driver_cus` 仅保留源码，不参与构建。
- **mipp**：手写笔（MIPP）shim 模块，本仓库自有实现。

## 2. 许可

仓库根目录的 `LICENSE` 为 **GPL-2.0**，适用于本仓库自有部分（`tools/`、`docs/`、
构建文件、`mipp/`）。仓库内聚合的第三方组件按各自声明授权：

| 范围 | 许可 |
|---|---|
| 本仓库自有部分（`tools/`、`docs/`、`mipp/`、构建文件） | GPL-2.0（见 `LICENSE`） |
| MTK 厂商驱动主体（`kernel_device_modules-6.1/`、`vendor/`） | 以各文件头部声明为准，GPL-2.0 与 Proprietary 混合 |
| `hbt_driver`、`hbt_driver_cus` | GPL-2.0 |
| `connectivity/wlan/core/gen4m` | Dual BSD / GPL |
| `fpsgo_cus` | Proprietary（客户定制层） |

**注意**：仓库内含 Proprietary 组件，GPL-2.0 不能覆盖它们。分发或再发布前，
请自行确认各组件条款；`LICENSE` 仅对本仓库自有部分作出授权。本仓库用于自有
设备的维护与调试。

## 3. 预编译对象

部分模块随源码提供预编译 `.o`（`.o_shipped`、`.cmd_shipped`），用于规避
闭源部分的重新编译。`.gitignore` 已对这类文件做例外放行，其余构建产物不入库。

## 4. 第三方代码

`kernel_device_modules-6.1/` 与 `vendor/` 下包含 Linux 内核衍生代码，其版权归
各自作者所有，遵循相应的 GPL-2.0 条款。本仓库不对这些代码主张任何权利。
