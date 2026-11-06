# vendor_dlkm 构建与分发说明

本文件说明 `//common-modules/mediatek` 这套 Kleaf 接线如何产出模块与 `vendor_dlkm` 镜像，
以及与原厂 ROM 之间的已知差异。

## 1. 常用命令

```bash
# 全量构建：内核 + 全部厂商模块（约 25 分钟，增量构建会快很多）
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install

# 构建 vendor_dlkm 镜像（erofs）
tools/bazel build //common-modules/mediatek:mgk_64_k61_images

# 分发到 <workspace>/out/kernel_modules（flat，会先清空该目录）
tools/bazel run //common-modules/mediatek:mgk_64_k61_dist

# 裁剪版镜像：仅打包加载清单内的 184 个模块，压缩 erofs（约 16 MB）
bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh
```

注意：`mgk_64_k61` 是 `kernel_build`，不是可执行目标，`bazel run` 会报
`Not executable`；只有本文件底部定义的 `mgk_64_k61_dist` 可以用 `run`。

## 2. 产物

`out/kernel_modules/` 下：

| 文件 | 说明 |
|---|---|
| `Image` / `Image.gz` / `Image.lz4` | 内核镜像（`mgk_64_k61_kernel_aarch64`） |
| `System.map` / `vmlinux` / `vmlinux.symvers` | 调试与符号信息 |
| `*.ko`（约 717 个） | 全部已构建模块，已 strip |
| `vendor_dlkm.img` | erofs 格式的 vendor_dlkm 分区镜像 |
| `vendor_dlkm.modules.load` | 镜像内的加载清单 |
| `vendor_dlkm.modules.blocklist` | 镜像内的 blocklist |

## 3. modules.load 的维护

`common-modules/mediatek/vendor_dlkm_modules.load` 是**全路径**清单
（如 `extra/common-modules/mediatek/kernel_device_modules-6.1/drivers/.../x.ko`），
由原厂 ROM 的 `modules.load`（basename 形式）与本次构建实际产出的模块名求交集生成。

必须用全路径而不是 basename：`build_utils.sh` 对清单做的是
`grep -w -f list modules.order`，basename 会因 `-` 不是单词字符而误匹配，
例如 `vcp.ko` 会命中 `mtk-mmdebug-vcp.ko`，`mtk-pwm.ko` 会命中 `leds-mtk-pwm.ko`。

模块增删后重新生成：

```bash
STAGE=bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging/lib/modules
REL=$(ls "$STAGE")
cd "$STAGE/$REL"
find kernel extra -name '*.ko' | sort > /tmp/all_ko.txt
sed 's|.*/||' <原厂 modules.load> | grep '\.ko$' | sort -u > /tmp/rom_base.txt
awk 'NR==FNR{keep[$0]=1; next} {n=split($0,a,"/"); if (a[n] in keep) print}' \
    /tmp/rom_base.txt /tmp/all_ko.txt > ../../../../../../vendor_dlkm_modules.load
```

## 4. 与原厂 ROM 的已知差异

原厂 `modules.load` 187 项，本树可构造 184 项。缺失 3 项，且均**无源码**：

| 模块 | 结论 |
|---|---|
| `wlan_drv_gen4m_6897.ko` | MTK connectivity 独立仓库（`vendor/mediatek/kernel_modules/connectivity/wlan/core/gen4m`），本交付包不含 `wlan/` 目录 |
| `fpsgo.ko` | MediaTek FPSGO（Proprietary，depends=mtk_fpsgo），与树内 `fpsgo_v3/mtk_fpsgo.ko` 不是同一模块 |
| `hbt.ko` | GPL，misc 设备 + `mmap_lock` trace hook，本树无源码 |

另有 2 个模块不在 `modules.load` 中、但原厂镜像里存在，同样无源码：
`msync2_frd.ko`（MediaTek MSYNC2，Proprietary，depends=mediatek-drm）、
`scheduler_ext.ko`（MediaTek scheduler，Proprietary）。

`pmic_tia.ko` 原本因 `CONFIG_MTK_PMIC_TIA` 未开启而不产出，已在
`kernel_device_modules-6.1/arch/arm64/configs/mgk_64_k61_defconfig` 中打开并加入
`kleaf_modules.bzl` 的 `MGK_64_K61_DEVICE_MODULES`。

## 5. 体积与裁剪

| 镜像 | 大小 | 内容 |
|---|---|---|
| `vendor_dlkm.img`（Kleaf 默认） | 176 MB | 全部 716 个模块，未压缩 erofs |
| `vendor_dlkm_trim.img`（裁剪） | **16 MB** | 仅加载清单内 184 个模块，lz4hc 压缩 erofs |
| 原厂镜像 | 15 MB | 206 个模块，压缩 erofs |

裁剪版由 `tools/make_vendor_dlkm_trim.sh` 生成：按 `vendor_dlkm_modules.load` 从
`out/kernel_modules` 取文件、重建 `lib/modules/<release>/` 树、用 prebuilts 里的
`depmod` 生成 `modules.dep/alias/softdep`、再用 `mkfs.erofs -zlz4hc,9` 打包。

同 184 个模块未压缩时约 48 MB，压缩后 16 MB；与原厂的差距主要来自编译选项
（本树模块平均体积约为原厂的 1.6 倍，例如 `camera_eeprom_v4l2.ko` 977 KB vs 165 KB）。

注意：压缩 erofs 依赖内核 `CONFIG_EROFS_FS_ZIP`。本设备原厂镜像即为压缩 erofs，
可直接使用。

直接替换分区时仍需确认目标分区容量：`vendor_dlkm` 是动态分区，镜像大小会改变分区尺寸。

