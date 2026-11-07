# MTK MT6897 内核模块工程（TB375FC）

本仓库是 `TB375FC` Kleaf 构建工作区中的**厂商内核模块**部分，与 `TB375FC/common`
（Linux 6.1.138 + MTK 合并内核）配套，用于产出 `vendor_dlkm` 分区所需的 `.ko`。

目标设备：MT8792 / MT6897 平台平板（`ro.product.device=TB375FC`，DT compatible
`mediatek,MT6897`）。构建产物与设备现网内核（6.1.140-android14-11）在 ABI 层兼容，
细节见第 7 节。

---

## 1. 目录结构

```
common-modules/mediatek/
├── BUILD.bazel                     # Kleaf 目标：mgk_64_k61 内核/模块/镜像/dist
├── mgk_ko.bzl                      # define_mgk_ko()：单个模块的 Kleaf 封装
├── build.config.mgk.aarch64        # 厂商 profile 的构建配置（模块构建入口）
├── vendor_dlkm_modules.load        # vendor_dlkm 的加载清单（全路径，187 项）
├── tools/make_vendor_dlkm_trim.sh  # 裁剪并打包 vendor_dlkm 镜像
├── README.vendor_dlkm.md           # vendor_dlkm 打包细节
├── kernel_device_modules-6.1/      # MTK 设备驱动主体（BSP 交付件）
└── vendor/mediatek/kernel_modules/ # 独立模块仓库集合
    ├── connectivity/{bt,common,connfem,conninfra,fmradio,gps,wlan}
    ├── gpu/                        # Mali (r25p0 ~ r44p1)
    ├── mtkcam/                     # 相机（camsys/imgsensor/imgsys/…）
    ├── fpsgo_cus/                  # FPSGO 客户层（引入）
    ├── hbt_driver/                 # HBT 驱动（引入）
    ├── hbt_driver_cus/             # HBT 客户变体（未接入）
    ├── met_drv_v2/ met_drv_v3/ udc/ zram_ext/ extend_reclaim/
```

---

## 2. 模块来源

| 来源 | 版本 / 提交 | 覆盖范围 |
|---|---|---|
| 设备厂商 BSP 交付包（`kernel-6.1.138/kernel`） | Linux 6.1.138 | `kernel_device_modules-6.1/` 与 `vendor/` 的原始主体 |
| `mt6897-devs/android_vendor_mediatek_kernel_modules` | `lineage-23.2` @ `6bceb1b8e3444dd6caf80ad7470c9e57bbdcd1ab` | `connectivity/wlan/core/gen4m`、`hbt_driver`、`hbt_driver_cus` |
| `MiCode/MTK_kernel_modules` | `bsp-duchamp-u-oss` @ `e57cdab505850464e0179c8cb16d17b09cf8c524` | `fpsgo_cus` |

引入说明：

- **wlan gen4m**：BSP 包内 `connectivity/wlan/core/gen4m` 是悬空软链，实际驱动缺失。
  该驱动是 `wlan_drv_gen4m_6897.ko`（CONNAC2X2_SOC7_0）的唯一来源，从同 SoC 的
  lineage-23.2 分支补齐。
- **hbt_driver**：BSP 包内无 `hbt_driver` 目录。`hbt.ko` 是 GPL 的 misc 设备 +
  `mmap_lock` 跟踪驱动。
- **fpsgo_cus**：BSP 包内只有 `performance/fpsgo_v3`（产出 `mtk_fpsgo.ko`），
  `fpsgo.ko` 是其客户定制层，`depends=mtk_fpsgo`。
- **hbt_driver_cus**：与 `hbt_driver` 同样产出 `hbt.ko`，两者互斥；当前接入
  `hbt_driver`，`hbt_driver_cus` 保留源码未参与构建。

上述来源均为公开仓库，许可证以各目录内的声明为准（GPL / Dual BSD-GPL /
Proprietary 混合）。本仓库仅用于自有设备的维护与调试。

---

## 3. Kleaf / Bazel 工具链建立

### 3.1 工作区构成

本目录必须位于 Kleaf 工作区内才能构建，工作区至少需要：

```
<workspace>/
├── WORKSPACE -> build/kernel/kleaf/bazel.WORKSPACE
├── build/kernel/                     # kleaf（bazel.sh、bazel.WORKSPACE、BUILD 规则）
├── build/bazel_common_rules/         # copy_to_dist_dir 等
├── common/                           # 内核源码（6.1.138）
├── common-modules/mediatek/          # 本仓库
└── prebuilts/{clang,build-tools,kernel-build-tools,jdk,bazel,...}
```

官方方式是用 `repo` 拉取 AOSP Kleaf 工作区，再替换 `common` 与 `common-modules`：

```bash
repo init -u https://android.googlesource.com/kernel/manifest \
  -b common-android14-6.1-2025-07
repo sync -j8
```

本工作区对应的 manifest 基线：`kernel/superproject` = `common-android14-6.1-2025-07`，
`build/kernel` = `main-kernel-build-2023`。

### 3.2 工具链版本

| 组件 | 版本 | 位置 |
|---|---|---|
| Clang / LLD | `clang-r487747c`（17.0.2） | `prebuilts/clang/host/linux-x86/` |
| Bazel | 由 `tools/bazel` 包装（`build/kernel/kleaf/bazel.sh`） | `prebuilts/bazel/linux-x86_64/` |
| 镜像工具 | `mkfs.erofs`、`mkuserimg_mke2fs`、`depmod`、`avbtool` | `prebuilts/kernel-build-tools/linux-x86/bin/` |
| JDK | 11 | `prebuilts/jdk/jdk11/` |

`tools/bazel` 是软链，指向 `build/kernel/kleaf/bazel.sh`；它负责设置
`--output_user_root=out/bazel/output_user_root` 等参数，不要直接调用系统 bazel。

### 3.3 本仓库的构建目标

| 目标 | 作用 |
|---|---|
| `//common-modules/mediatek:mgk_64_k61_kernel_aarch64` | 厂商 profile 的内核（notrim，导出非 KMI 符号） |
| `//common-modules/mediatek:mgk_64_k61` | 模块准备用的 kernel_build（`make modules`） |
| `//common-modules/mediatek:mgk_64_k61_modules` | 全部模块的 kernel_module_group |
| `//common-modules/mediatek:mgk_64_k61_modules_install` | 安装到 staging 目录 |
| `//common-modules/mediatek:mgk_64_k61_images` | `vendor_dlkm.img`（erofs） |
| `//common-modules/mediatek:mgk_64_k61_dist` | 分发到 `out/kernel_modules`（可 `bazel run`） |

```bash
# 全量构建（首次约 25 分钟，增量很快）
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install

# 分发：Image / *.ko / vendor_dlkm.img → <workspace>/out/kernel_modules
tools/bazel run //common-modules/mediatek:mgk_64_k61_dist

# 裁剪版镜像（只含加载清单内的模块，约 18 MB）
bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh
```

注意：`mgk_64_k61` 是 `kernel_build`，不可 `run`；可执行的只有 `mgk_64_k61_dist`。

### 3.4 配置来源

`build.config.mgk.aarch64` 定义 `KERNEL_DIR=common`，并在 `PRE_DEFCONFIG_CMDS` 中把
`gki_defconfig` + `kernel_device_modules-6.1/arch/arm64/configs/mgk_64_k61_defconfig`
+ `userdebug.config` 合并为最终配置。

新增模块所需的 `CONFIG_*` 应加在 `mgk_64_k61_defconfig`；
内核侧不存在的符号通过 `kernel_device_modules-6.1/Kconfig.ext` 补充。

---

## 4. 新增一个模块

1) 源码放入 `vendor/mediatek/kernel_modules/<name>/`，目录内提供 `Makefile`（递归驱动）
   与 `Kbuild`（`obj-m` 定义）。例如 wlan 的写法：

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

4) 若该模块需要进入 `vendor_dlkm` 的加载列表，重新生成
   `vendor_dlkm_modules.load`（见 `README.vendor_dlkm.md` 第 3 节）。

---

## 5. 设备树

板级 DTS 位于
`kernel_device_modules-6.1/arch/arm64/boot/dts/mediatek/tb8792p1_64_wifi/`，
由 `mgk_64_k61` 的 kernel_build 一并编译。

---

## 6. 构建产物

`out/kernel_modules/`（`mgk_64_k61_dist` 输出，flat）：

| 文件 | 说明 |
|---|---|
| `Image` / `Image.gz` / `Image.lz4` | 内核镜像 |
| `System.map` / `vmlinux` / `vmlinux.symvers` | 调试符号 |
| `*.ko`（720 个） | 全部已编译模块（已 strip） |
| `vendor_dlkm.img` | 全量 erofs 镜像 |
| `vendor_dlkm_trim.img` | 裁剪后镜像（187 个加载模块，lz4hc 压缩） |

---

## 7. 已知限制与注意事项

1. **wlan 的 Kbuild 入口被改造**：`connectivity/wlan/core/gen4m/` 下的
   `Makefile` 与 `Kbuild` 是本工程重写的入口，原始文件保留为
   `Makefile.dispatch` / `Kbuild.dispatch`。原因是厂商脚本依赖 ALPS 的
   `$O/$M/DEVICE_MODULES_REL_DIR` 递归流程，与 Kleaf 的 `make -C <module_dir>`
   调用方式不兼容。新入口固定 `MODULE_NAME=wlan_drv_gen4m_6897`、`SEGMENT=SP`。

2. **wlan 的 modpost 告警**：`wlan_drv_gen4m_6897.ko` 引用 `connadp.ko` 导出的
   `conn_pwr_*`、`aee_*`、`register_*_handler` 等 10 个符号，构建期不可见。
   构建命令里加了 `KBUILD_MODPOST_WARN=1` 放行；运行期由 `connadp.ko` 提供
   （与原厂模块 `depends=connadp,wmt_chrdev_wifi_connac2,…` 一致）。
   若后续出现真实的符号缺失，该开关会掩盖问题，需要逐项核对 `depends`。

   副作用：本工程产出的 `wlan_drv_gen4m_6897.ko` 的 `depends` 只有 `cfg80211`，
   原厂为 `connadp,wmt_chrdev_wifi_connac2,aee_aed,cfg80211,mtk_disp_notify,
   pmic_lbat_service,conninfra,connfem`。也就是说 `modules.dep` 里的依赖边不完整，
   **不能依赖 modprobe 自动拉起依赖**。当前 `vendor_dlkm_modules.load` 的顺序
   （`connadp` → `connfem` → `conninfra` → `wmt_chrdev_wifi_connac2` → `wlan`）
   满足加载顺序要求，Android init 按该顺序 insmod 即可。若要恢复完整 `depends`，
   需要为这些模块提供 `KBUILD_EXTRA_SYMBOLS`（即把 connadp 等作为真正的 deps 接入）。

3. **模块体积**：同一批模块本工程编出的体积约为原厂的 1.6 倍（编译选项差异，
   非源码问题）。未压缩镜像 48 MB、压缩后 18 MB，原厂为 15 MB。

4. **内核 sublevel 差异**：本目录产出的模块 `vermagic` 为
   `6.1.138-android14-11-maybe-dirty`，设备现运行内核为
   `6.1.140-android14-11-maybe-dirty`。由于 `CONFIG_MODVERSIONS=y`，
   `same_magic()` 只比较 flag 段与符号 CRC，版本前缀不一致不影响加载。

5. **未在设备上验证本仓库产出的镜像**：本文档中的产物均只在构建侧校验
   （`fsck.erofs` 解包、`vermagic`、加载清单比对），未刷入设备。

6. **镜像必须带 SELinux 标签（已踩坑）**：首次刷入早期版本镜像（只用
   `mkfs.erofs --all-root` 打包）后，设备卡在 logo，无法进入开机动画。
   `logcat` 中的决定性证据是：

   ```
   avc: denied { search } for comm="init.insmod.sh" name="/" dev="dm-7"
        scontext=u:r:init_insmod_sh:s0 tcontext=u:object_r:unlabeled:s0 tclass=dir
   ```

   `dm-7` 即 `/vendor_dlkm`，其根目录为 `unlabeled`，导致模块加载器被 SELinux
   拒绝进入，所有 vendor 模块无法加载，SurfaceFlinger 起不来（因此停在 logo，
   与内核无关）。修正方式是在打包时按原厂参数补齐：

   ```bash
   mkfs.erofs -zlz4hc -T 0 --all-root \
     --mount-point=/vendor_dlkm \
     --fs-config-file=<逐路径 权限表> \
     --file-contexts=<逐路径 SELinux 上下文> \
     vendor_dlkm.img <staging_dir>
   ```

   `tools/make_vendor_dlkm_trim.sh` 已按此实现：上下文与 fs_config 均按实际
   staged 内容逐路径生成（模块 `u:object_r:vendor_file:s0`，`etc/` 下为
   `vendor_configs_file`），因为原厂的静态清单是按原内核 release 目录名列举的，
   sublevel 变化后会失配。

7. **目录布局必须扁平（同样踩过坑）**：原厂 `vendor_dlkm` 的模块直接位于
   `lib/modules/*.ko`，`modules.load` / `modules.dep` 也在同一层，
   `modules.dep` 内使用绝对路径 `/vendor_dlkm/lib/modules/xxx.ko`。
   若打成 `lib/modules/<release>/`（Kleaf `kernel_modules_install` 的默认布局），
   即使 SELinux 标签正确，init 也找不到 `modules.load`，**一个模块都不会加载**。
   `make_vendor_dlkm_trim.sh` 现在先按扁平结构 stage，再在临时版本目录里跑
   `depmod`，最后把 `modules.dep` 的路径改写为扁平的绝对路径。

8. **`modules.load` 必须沿用原厂顺序，不能按符号依赖重排（第三次踩坑）**：
   本树把 CMDQ / ADSP / AEE / SLBC / VCOREFS / EMI / DVFSRC 等子系统编成了独立
   模块，而原厂把它们内建在 vmlinux 里。因此"缺符号"看起来像是需要把这些
   provider 全部补进 `modules.load`。**这个做法会导致设备在 logo 阶段崩溃重启。**

   实测依据（在已正常启动的系统上逐个 `insmod` 验证）：

   | 模块 | 结果 |
   |---|---|
   | `ccci_util_lib` / `ccci_auxadc` / `ccci_md_all` / `ccmni` | 加载成功，`conninfra` 随之成功，`vendor.connsys.driver.ready=yes` 触发 BT 驱动 |
   | `zsmalloc` / `irq_monitor` | 加载成功 |
   | `ffa_v10` | 失败：`exports duplicate symbol ffa_bus_type (owned by ffa_module)` |
   | `mcDrvModule` | 失败：`exports duplicate symbol mc_close_device (owned by mcDrvModule_ffa)` |
   | `scpsys-dummy` | 失败：`Exec format error` |
   | `libarc4` | 失败：`Permission denied`（导出 KMI 保护符号） |

   关键事实：**这些 provider 绝大多数已经在设备上了**——vendor_boot 的 ramdisk
   提供 222 个模块（`mtk_dramc`、`emi`、`smpu`、`mtk_slbc`、`mtk-mml`、
   `mediatek-drm`、`aee_aed`、`mrdump`、`ffa_module`、`mcDrvModule_ffa`、
   `mtk_scpsys` 等），`/proc/modules` 可直接确认。把它们再塞进 `vendor_dlkm`
   的加载清单，对已加载者是空操作，对未加载者则是引入了**互斥的另一套实现**。

   真正缺的只有 CCCI 一簇（4 个模块）。它们提供 `get_smem_phy_start_addr`、
   `exec_ccci_kern_func` 等符号，是 `conninfra` → WiFi/BT/FM 的前置条件。

   因此 `modules.load` 由 `modules.load.stock`（原厂 187 项）加
   `modules.load.extra`（CCCI 4 项，插在 `connadp.ko` 之前）合并而成，
   合计 191 项；与原厂清单的差异只有这 4 行，顺序完全一致。

   **顺序本身编码硬件上电次序**（时钟 → pinctrl → PMIC → regulator →
   power domain → display）。`init.insmod.sh` 在 `early-init` 用
   `modprobe -a -d /vendor/lib/modules $(cat modules.load)` 一次性加载，
   依赖由 `modules.dep` 解析；顺序被重排后，早启动阶段的探测会失序，
   表现为 logo 后立即重启（不是 panic，是启动流程无法完成）。
   `tools/solve_deps.py` 只保留为离线诊断工具，**不要**用它的输出直接当加载清单。

---

## 8. system_dlkm 构建

```bash
# 依赖已构建好的 staging
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install
bash common-modules/mediatek/tools/make_system_dlkm.sh
# → out/kernel_modules/system_dlkm.img
```

要点：

- 模块集合来自原厂 `system_dlkm` 的 `modules.load`（60 项），从 staging 的
  `kernel/` 目录按名匹配后扁平 stage；
- 本 profile 下 `CONFIG_BT=y`、`CONFIG_RFKILL=y`，所以 `bluetooth.ko` / `rfkill.ko`
  不存在，最终是 58 个模块，属正常；
- SELinux 类型为 `u:object_r:system_file:s0`（vendor_dlkm 是 `vendor_file`）；
- 与 vendor_dlkm 同样要求扁平布局与绝对路径 `modules.dep`。

**三个镜像必须成组刷入**：`boot`、`system_dlkm`、`vendor_dlkm` 三者都来自
`mgk_64_k61` 这一套构建，`module_layout` 等符号 CRC 才一致。设备上曾经只替换
vendor_dlkm 而内核是 `//common:kernel_aarch64`（GKI 配置），结果所有厂商模块
insmod 报 `Exec format error`，内核打印 `disagrees about version of symbol module_layout`
——两者的 `module_layout` CRC 分别是 `0xea759d7f` 与 `0x9efa483d`。

## 9. ABI 一致性：module_layout CRC

内核与模块必须由**同一份配置**编译，否则内核加载模块时报
`disagrees about version of symbol module_layout`（表现为 `insmod: Exec format error`）。
`module_layout` 是 `struct module` 布局的哈希，任何改变该结构体的 `CONFIG_*` 都会让它变化。

实测数据：

| 构建 | module_layout CRC |
|---|---|
| GKI（`//common:kernel_aarch64`） | `0xea759d7f` |
| Lenovo 原厂 vendor 模块 | `0xea759d7f` |
| 本工程（`gki_defconfig` + `mgk_64_k61_defconfig`，无 userdebug） | `0xea759d7f` |
| 本工程（再合并完整 `userdebug.config`） | **`0x9efa483d`** |

结论：**`kernel/configs/userdebug.config` 会改变 `struct module` 布局**。因此
`build.config.mgk.aarch64` 不再合并它，改为只合并
`kernel/configs/mgk_debug_min.config`（仅保留会产出 `.ko` 的开关与 pstore 参数）。

这一条同时解释了为什么不能只换 vendor_dlkm：设备上的 `vendor_boot` ramdisk 内含
222 个模块，它们与原厂内核同样是 `0xea759d7f`，内核必须与之匹配，否则第一阶段就失败。

## 10. 许可

各子目录保留上游原始许可证声明（`SPDX-License-Identifier`）。整体以 GPL-2.0
为主，部分 MediaTek 组件为 Proprietary。使用与分发前请逐目录确认。
