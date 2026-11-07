# 镜像打包

三件套：`boot` + `system_dlkm` + `vendor_dlkm`，**必须同源同 profile**，否则
`module_layout` CRC 不一致，模块加载全部失败。

## 1. 产物一览

| 镜像 | 大小 | 内容 | 打包方式 |
|---|---|---|---|
| `boot.img` | 17.2 MB | 内核 `Image.lz4`（18.3 MB），header v4，无 ramdisk | `mkbootimg.py` |
| `system_dlkm.img` | 6.3 MB | 58 个 GKI 模块（来自 staging 的 `kernel/`） | `make_system_dlkm.sh` |
| `vendor_dlkm.img` | 19.7 MB | 271 个厂商模块（来自 `extra/`） | `make_vendor_dlkm_trim.sh` |

原厂对应为 15.4 MB / 7.3 MB（vendor_dlkm 只含 206 个模块）。本工程镜像略大，
原因是模块集更全且编译选项不同（单模块平均体积约为原厂的 1.6 倍）。

## 2. 分区契约（三条硬约束）

这三条都来自实机排查，违反任一条设备都会停在 logo：

### 2.1 扁平布局

```
/vendor_dlkm/lib/modules/
├── *.ko                     ← 模块直接在此层
├── modules.load             ← init 直接读这个路径
├── modules.dep              ← 内容为绝对路径 /vendor_dlkm/lib/modules/x.ko
└── modules.alias / modules.softdep / modules.order
```

若打成 `lib/modules/<release>/`（Kleaf `kernel_modules_install` 的默认布局），
即使标签正确，init 也找不到 `modules.load`，**一个模块都不会加载**。

### 2.2 SELinux 标签与权限

`mkfs.erofs` 必须带：

```bash
mkfs.erofs -zlz4hc -T 0 --all-root \
  --mount-point=/vendor_dlkm \
  --fs-config-file=<逐路径权限表> \
  --file-contexts=<逐路径 SELinux 上下文> \
  vendor_dlkm.img <staging_dir>
```

缺失后三项会让所有文件变成 `u:object_r:unlabeled:s0`，`init.insmod.sh` 被
`avc: denied` 拒绝，模块无法加载。

上下文与权限表由脚本按实际 staged 内容**逐路径生成**，不能沿用原厂静态清单——
原厂清单按原内核 release 目录名（`…gfdf3de8ccf1b`）逐文件列举，sublevel 变化即失配。

### 2.3 模块签名

打包阶段调用 `tools/sign_modules.sh`，用内核构建时的同一份
`certs/signing_key.pem/x509` 对每个 `.ko` 执行 `sign-file sha1`。

原因见 [PITFALLS.md](PITFALLS.md) 第 4 条：未签名模块导出 GKI 保护符号会被拒。

校验：

```bash
strings -a xxx.ko | grep 'Module signature appended'   # 命中 1 次即已签名
```

## 3. 加载清单的维护

### 3.1 两个不同用途的清单

| 文件 | 作用 | 谁读它 |
|---|---|---|
| `tools/vendor_dlkm/image_contents.load` | **镜像内容**：哪些 `.ko` 打进镜像 | 打包脚本 |
| `tools/vendor_dlkm/modules.load.stock` | 原厂开机加载顺序（187 项，基线，勿改） | 打包脚本 |
| `tools/vendor_dlkm/modules.load.extra` | 需插到原厂顺序中的补充模块（5 项） | 打包脚本 |
| `vendor_dlkm_modules.load`（仓库根） | Kleaf 自带 `kernel_images` 目标的输入，**全路径**格式 | `BUILD.bazel` |

根目录那份是给 Kleaf 的 `mgk_64_k61_images` 用的，格式必须是全路径：
Kleaf 内部对清单做 `grep -w -f list modules.order`，basename 会因 `-` 不是单词
字符而误匹配（`vcp.ko` 命中 `mtk-mmdebug-vcp.ko`）。它的产物未压缩、体积大，
**不用于刷机**，保留只是为了 `kernel_images` 目标可用。

打包脚本把 `stock` 与 `extra` 合并成镜像内的 `modules.load`（192 项），
**顺序与原厂逐行一致，差异只有被插入的那 5 行**。

### 3.2 加载机制

`init.insmod.sh` 在 `early-init` 执行：

```sh
modprobe -a -d /vendor/lib/modules $(cat modules.load)
```

- 依赖由 `modules.dep` 解析，`provider` 在镜像里就会被自动拉起，**不必写进清单**；
- 例外：`zram_ext.ko` / `extend_reclaim.ko` 被脚本单独 `insmod`（绕过 modprobe），
  所以 `zsmalloc.ko` 必须显式列入 `modules.load.extra`。

### 3.3 新增条目的判断

| 场景 | 做法 |
|---|---|
| 只随镜像提供，由 `.rc` 按需 `insmod` | 仅加入 `image_contents.load` |
| 需要开机自动加载 | 同时加入 `modules.load.extra` |
| 是别人的符号提供者 | 不必加入清单，`modprobe` 会按 `modules.dep` 自动拉起 |

## 4. boot 镜像

```bash
python3 tools/mkbootimg/mkbootimg.py --header_version 4 \
  --kernel out/kernel_modules/Image.lz4 \
  --output out/kernel_modules/boot.img
```

要点：

- **内核必须是 LZ4 压缩态**。原厂 boot 的内核段前 4 字节为 `02 21 4c 18`，
  若误用未压缩的 `Image`（38 MB），引导器解压失败，直接停在 bootloader，
  连 fastbootd 都进不去。
- header v4、无 ramdisk、无 cmdline，与原厂 boot 头一致。

## 5. 刷入与回滚

```bash
adb reboot bootloader
fastboot reboot fastboot                      # 动态分区必须进 fastbootd
fastboot flash boot         boot.img
fastboot flash system_dlkm  system_dlkm.img
fastboot flash vendor_dlkm  vendor_dlkm.img
fastboot reboot
```

镜像大小变化会自动触发逻辑分区 resize（`Resizing 'vendor_dlkm_a'`）。
建议每次刷入前把当前可启动的三个镜像备份到本地，作为回滚基线。

## 6. 产物清单

`out/kernel_modules/`（`mgk_64_k61_dist` 输出，flat）：

| 文件 | 说明 |
|---|---|
| `Image` / `Image.gz` / `Image.lz4` | 内核镜像（38 MB / 18.3 MB） |
| `System.map` / `vmlinux` / `vmlinux.symvers` | 调试与符号信息 |
| `*.ko`（720 个） | 全部已编译模块（已 strip） |
| `vendor_dlkm.img` | Kleaf 自带的全量镜像（未压缩，体积大，不建议刷） |
