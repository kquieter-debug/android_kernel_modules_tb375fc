# MT6897 内核模块工程（TB375FC）

[English](README.md) | **简体中文**

MT8792 / MT6897 平台平板（`ro.product.device=TB375FC`）的**厂商内核模块**工程，
以 Kleaf 构建，产出 `vendor_dlkm`、`system_dlkm` 所需的 `.ko` 与分区镜像。

与 `TB375FC/common`（Linux 6.1.138 + MTK 合并内核）配套使用。内核与模块
**必须出自同一 profile**，否则 `module_layout` CRC 不一致，`insmod` 会返回
`Exec format error`。

## 当前状态

| 项 | 值 |
|---|---|
| 内核 | 6.1.138-android14-11（`mgk_64_k61` profile） |
| `module_layout` CRC | `0xea759d7f`（与原厂 GKI、`vendor_boot` 内 222 个模块一致） |
| 编译出的模块 | 720 个 |
| `vendor_dlkm` 镜像内容 | 271 个模块 |
| 开机加载清单 | 192 项，**实机 192/192 全部加载成功** |
| 设备侧模块总数 | 404 个 |
| 已验证功能 | 显示、触摸、GPU、WiFi、BT、音频、视频编解码、相机、传感器、ZRAM |

## 快速开始

以下命令在 Kleaf 工作区根目录（含 `WORKSPACE`、`common/`、`common-modules/`）执行：

```bash
# 1) 编译内核与全部模块（首次约 25 分钟）
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install

# 2) 分发到 <workspace>/out/kernel_modules
tools/bazel run //common-modules/mediatek:mgk_64_k61_dist

# 3) 打包两个分区镜像（内含模块签名）
bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh
bash common-modules/mediatek/tools/make_system_dlkm.sh

# 4) 打包 boot（header v4、无 ramdisk、内核需为 LZ4 压缩）
python3 tools/mkbootimg/mkbootimg.py --header_version 4 \
  --kernel out/kernel_modules/Image.lz4 --output out/kernel_modules/boot.img
```

> `mgk_64_k61` 是 `kernel_build`（分析型目标），**不可 `run`**；可执行目标只有
> `mgk_64_k61_dist`。

## 仓库结构

```
common-modules/mediatek/
├── BUILD.bazel                      Kleaf 目标：内核 / 模块 / 镜像 / dist
├── mgk_ko.bzl                       define_mgk_ko()：单个模块的 Kleaf 封装
├── build.config.mgk.aarch64         厂商 profile 构建配置
├── kernel_device_modules-6.1/       MTK 设备驱动主体（BSP 交付件）
├── vendor/mediatek/kernel_modules/  独立模块集合（connectivity / gpu / mtkcam / …）
├── mipp/                            手写笔（MIPP）shim 模块
├── tools/
│   ├── make_vendor_dlkm_trim.sh     vendor_dlkm 镜像打包
│   ├── make_system_dlkm.sh          system_dlkm 镜像打包
│   ├── sign_modules.sh              模块签名（由上面两个脚本调用）
│   ├── solve_deps.py                符号依赖离线诊断（不用于生成加载清单）
│   └── vendor_dlkm/ system_dlkm/    清单、SELinux 上下文、权限表
└── docs/                            详细文档
```

## 文档

| 文档 | 内容 |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | 工作区构成、工具链版本、构建目标、配置来源、新增模块流程 |
| [docs/IMAGES.md](docs/IMAGES.md) | 两个 dlkm 镜像与 boot 的打包细节、加载清单维护、签名 |
| [docs/PITFALLS.md](docs/PITFALLS.md) | **踩坑清单**：SELinux 标签、扁平布局、加载顺序、依赖污染、签名 |
| [NOTICE.md](NOTICE.md) | 模块来源、上游提交号、许可证说明 |

> `docs/` 目前为中文；如需英文版可以翻译。

## 三条不能违背的约束

这三条都是实机验证得出的，违反任何一条都会导致设备停在 logo：

1. **模块目录必须扁平**：`lib/modules/*.ko` 与 `modules.load`、`modules.dep` 同层，
   `modules.dep` 内用绝对路径 `/vendor_dlkm/lib/modules/xxx.ko`。
2. **`modules.load` 必须沿用原厂顺序**：顺序编码了硬件上电次序，不能按符号依赖重排。
3. **模块必须签名**：未签名模块只要导出 GKI 保护符号就会被内核拒绝（`EACCES`）。

细节与排查方法见 [docs/PITFALLS.md](docs/PITFALLS.md)。

## 许可

本仓库自有部分（`tools/`、`docs/`、`mipp/`、构建文件）按根目录
[LICENSE](LICENSE) 授权（GPL-2.0）。聚合进来的第三方组件（MTK 厂商驱动、上游模块）
按各自目录内的声明为准，为 GPL / Dual BSD-GPL / Proprietary 混合，GPL-2.0 不覆盖它们。

来源与上游提交号见 [NOTICE.md](NOTICE.md)。
