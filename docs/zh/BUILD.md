# 构建

## 1. 工作区构成

本目录必须位于 Kleaf 工作区内才能构建。工作区至少需要：

```
<workspace>/
├── WORKSPACE -> build/kernel/kleaf/bazel.WORKSPACE
├── build/kernel/                     kleaf（bazel.sh、bazel.WORKSPACE、构建规则）
├── build/bazel_common_rules/         copy_to_dist_dir 等
├── common/                           内核源码（6.1.138）
├── common-modules/mediatek/          本仓库
└── prebuilts/{clang,build-tools,kernel-build-tools,jdk,bazel,…}
```

官方方式是用 `repo` 拉取 AOSP Kleaf 工作区，再替换 `common` 与 `common-modules`：

```bash
repo init -u https://android.googlesource.com/kernel/manifest \
  -b common-android14-6.1-2025-07
repo sync -j8
```

本工作区对应的 manifest 基线：`kernel/superproject` = `common-android14-6.1-2025-07`，
`build/kernel` = `main-kernel-build-2023`。

## 2. 工具链版本

| 组件 | 版本 | 位置 |
|---|---|---|
| Clang / LLD | `clang-r487747c`（17.0.2） | `prebuilts/clang/host/linux-x86/` |
| Bazel | 由 `tools/bazel` 包装 | `prebuilts/bazel/linux-x86_64/` |
| 镜像工具 | `mkfs.erofs`、`depmod`、`avbtool` | `prebuilts/kernel-build-tools/linux-x86/bin/` |
| JDK | 11 | `prebuilts/jdk/jdk11/` |
| 签名工具 | `scripts/sign-file`（本地 gcc 编译） | `tools/bin/`（gitignore） |

`tools/bazel` 是软链，指向 `build/kernel/kleaf/bazel.sh`，负责设置
`--output_user_root=out/bazel/output_user_root`。**不要直接调用系统 bazel。**

## 3. 构建目标

| 目标 | 作用 |
|---|---|
| `//common-modules/mediatek:mgk_64_k61_kernel_aarch64` | 内核（notrim，导出非 KMI 符号） |
| `//common-modules/mediatek:mgk_64_k61` | 模块准备用 kernel_build（`make modules`） |
| `//common-modules/mediatek:mgk_64_k61_modules` | 全部模块的 kernel_module_group |
| `//common-modules/mediatek:mgk_64_k61_modules_install` | 安装到 staging 目录 |
| `//common-modules/mediatek:mgk_64_k61_images` | Kleaf 自带的 `vendor_dlkm.img`（未启用压缩，体积偏大） |
| `//common-modules/mediatek:mgk_64_k61_dist` | 分发到 `out/kernel_modules`（唯一可 `run` 的目标） |

## 4. 配置来源

`build.config.mgk.aarch64` 定义 `KERNEL_DIR=common`，并在 `PRE_DEFCONFIG_CMDS` 中
按顺序合并三份配置：

```
gki_defconfig
  + kernel_device_modules-6.1/arch/arm64/configs/mgk_64_k61_defconfig
  + kernel_device_modules-6.1/kernel/configs/mgk_debug_min.config
```

### 为什么第三份不是 `userdebug.config`

原 BSP 使用 `kernel/configs/userdebug.config`，但该文件会改变 `struct module`
布局，使内核的 `module_layout` 变为 `0x9efa483d`，而原厂内核、原厂
`system_dlkm` 以及 `vendor_boot` 内 222 个模块期望的是 `0xea759d7f`。

结果就是任何模块 `insmod` 都报：

```
disagrees about version of symbol module_layout   （用户态表现为 Exec format error）
```

`mgk_debug_min.config` 只保留会产出 `.ko` 的四个开关
（`MTK_IRQ_MONITOR`、`MTK_SELINUX_AEE_WARNING`、`MEDIATEK_CPUFREQ_DEBUG`、
`PSTORE_BLK`）与 pstore 参数。替换后 CRC 与原厂精确一致。

> 结论：**新增 `CONFIG_*` 前先确认它是否影响 `struct module` 或 KMI CRC**，
> 影响则必须重新对齐基线。

新增模块所需的 `CONFIG_*` 加在 `mgk_64_k61_defconfig`；内核侧不存在的符号
通过 `kernel_device_modules-6.1/Kconfig.ext` 补充。

## 5. 新增一个模块

1) 源码放入 `vendor/mediatek/kernel_modules/<name>/`，目录内提供 `Makefile`
   （递归驱动）与 `Kbuild`（`obj-m` 定义）：

```make
# Makefile：Kleaf 以 make -C <module_dir> 调用，需提供 all/modules_install/clean
all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
```

2) 添加 `BUILD.bazel`：

```python
load("//common-modules/mediatek:mgk_ko.bzl", "define_mgk_ko")

define_mgk_ko(
    name = "foo",
    outs = ["foo.ko"],
    # deps = ["//…:conninfra"],   # 需要其它模块的 Module.symvers 时
)
```

3) 把目标加入 `kernel_device_modules-6.1/kleaf_modules.bzl` 的
   `MGK_64_K61_KLEAF_MODULES`（按路径排序）。

4) 若要进入开机加载集，在 `tools/vendor_dlkm/modules.load.extra` 增加条目；
   若只是随镜像提供、由 `.rc` 按需加载，只需加入 `image_contents.load`。
   两者的区别见 [IMAGES.md](IMAGES.md)。

## 6. 设备树

板级 DTS 位于
`kernel_device_modules-6.1/arch/arm64/boot/dts/mediatek/tb8792p1_64_wifi/`，
由 `mgk_64_k61` 的 kernel_build 一并编译。
