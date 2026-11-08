# 踩坑清单

每条都是实机验证过的，格式统一为：现象 → 证据 → 根因 → 修法。
按"会导致设备停在 logo"的优先级排序。

---

## 1. 模块目录必须是扁平布局

**现象**：刷入后设备停在 logo，`vendor_dlkm` 已挂载、SELinux 标签正确，
但 `/proc/modules` 里一个 vendor 模块都没有。

**证据**：设备上 `/vendor_dlkm/lib/modules/` 下只有一个子目录
`6.1.138-android14-11-maybe-dirty/`。

**根因**：`init` 直接读 `/vendor_dlkm/lib/modules/modules.load`，不会进版本子目录找。
Kleaf `kernel_modules_install` 的默认布局是 `lib/modules/<release>/`，与分区契约不符。

**修法**：所有 `.ko` 扁平 stage 到 `lib/modules/`；`depmod` 仍在临时版本目录里跑，
再把 `modules.dep` 的路径改写为绝对形式 `/vendor_dlkm/lib/modules/x.ko`。

---

## 2. 镜像必须带 SELinux 标签

**现象**：设备停在 logo，`init.insmod.sh` 加载失败。

**证据**：

```
avc: denied { search } for comm="init.insmod.sh" name="/" dev="dm-7"
     scontext=u:r:init_insmod_sh:s0 tcontext=u:object_r:unlabeled:s0 tclass=dir
```

`dm-7` 即 `/vendor_dlkm`，根目录为 `unlabeled`。

**根因**：`mkfs.erofs` 少了 `--mount-point` / `--fs-config-file` /
`--file-contexts`，inode 上没有 `security.selinux` 扩展属性。

**修法**：补齐三个参数，并按实际内容逐路径生成上下文与权限表
（模块 `vendor_file`，`etc/` 下 `vendor_configs_file`）。原厂静态清单按原内核
release 目录名列举，sublevel 变化即失配，不能直接复用。

---

## 3. `modules.load` 必须沿用原厂顺序

**现象**：加入一批"符号提供者"模块后，设备在 logo 后**崩溃重启**。

**证据**：对比发现清单从 187 项扩到 274 项，其中 89 项是原厂不加载的模块；
把顺序换成按符号依赖拓扑排序后触发。

**根因**：顺序本身编码了硬件上电次序（时钟 → pinctrl → PMIC → regulator →
power domain → display）。`modprobe -a` 一次性加载，顺序被重排后早启动阶段的
探测失序，启动流程无法完成（不是 panic，是流程走不完）。

**修法**：`modules.load` = 原厂 `modules.load.stock`（187）+ 必要补入
`modules.load.extra`（5），插入点固定为 `connadp.ko` 之前。
`tools/solve_deps.py` 只作离线诊断，**不要**拿它的输出当加载清单。

---

## 4. 模块必须签名

**现象**：个别模块 `insmod` 返回 `Permission denied`，其余模块正常。

**证据**：内核 `kernel/module/main.c`：

```c
if (!mod->sig_ok && gki_is_module_protected_export(sym)) {
        pr_err("%s: exports protected symbol %s\n", ...);
        return -EACCES;
}
```

命中：`libarc4`（`arc4_setkey`/`arc4_crypt`）、`mii`（`generic_mii_ioctl`），
后者连带 `usbnet` → `smsc75xx/95xx` 全链失效。

**根因**：所有模块未签名，`sig_ok` 为假。

**两条错路（勿重试）**：

| 尝试 | 结果 |
|---|---|
| `CONFIG_CRYPTO_LIB_ARC4=y` | 无效，`net/mac80211/Kconfig` 的 `select` 把强度钉在 `=m` |
| `CONFIG_MODULE_SIG_ALL=y` | 构建失败，`Makefile.modinst` 需要 `certs/signing_key.x509`，Kleaf 沙箱生成但不导出 |

**修法**：打包阶段由 `tools/sign_modules.sh` 用内核构建的同一份
`certs/signing_key.pem/x509` 逐个 `sign-file sha1`。密钥一致，故
`sig_ok` 为真。

---

## 5. 不要打包「替代实现」模块（会污染 `modules.dep`）

**现象**：设备停在 logo，`init.svc.surfaceflinger=restarting`，
SurfaceFlinger 崩在 `SkiaGLRenderEngine::chooseEglConfig`（EGL 拿不到配置），
`/dev/mali0` 不存在。但失败模块是散开分布的，不是"从某一条开始全失败"。

**证据**：`mali_kbase`、`ged`、`gz_main_mod`、`tmem_ffa`、`mtk_sec_heap`
逐个手动 `insmod` **全部 rc=0** —— 模块本身没坏。

`modules.dep` 里：

```
mali_kbase_mt6897_r44.ko: ged.ko … tmem_ffa.ko ffa_v10.ko mcDrvModule.ko …
```

而 `ffa_v10` / `mcDrvModule` 因重复符号无法加载：

```
ffa_v10:     exports duplicate symbol ffa_bus_type (owned by ffa_module)
mcDrvModule: exports duplicate symbol mc_close_device (owned by mcDrvModule_ffa)
```

**根因**：本树为三个子系统各产出两套实现，设备实际运行的是 vendor_boot 那套。
`depmod` 把本树产出记进了别的模块的 `depends`，`modprobe` 必须先满足依赖，
于是拉一个必然失败的模块，整条链失败。

| 本树产出 | 设备实际使用 | 受影响的下游 |
|---|---|---|
| `ffa_v10.ko` | `ffa_module.ko` | mali_kbase、gz_main_mod、tmem_ffa、mtk_sec_heap、mtk_disp_sec、widevine_driver、sapu、t-base-tui、mtk_heap_debug、mtk-cam-isp7sp |
| `mcDrvModule.ko` | `mcDrvModule_ffa.ko` | 同上 |
| `scpsys-dummy.ko` | `mtk_scpsys*.ko` | 加载失败（ENOEXEC） |

**修法**：从 `image_contents.load` 剔除这三个。剔除后 `modules.dep` 不再引用它们，
未定义符号在 `insmod` 时直接由已加载的 vendor_boot 模块解析。

**通用判据**：凡"本树产出、但设备上已有同功能另一实现"的模块，都不应进入镜像。
判定方法：在运行系统上 `insmod` 该模块，若报 `exports duplicate symbol` 或
`File exists`，即属此类。

---

## 6. `module_layout` CRC 必须与原厂一致

**现象**：模块 `insmod` 报 `Exec format error`。

**证据**：

```
mtk_lpm: disagrees about version of symbol module_layout
```

| 来源 | CRC |
|---|---|
| 原厂 vendor 模块 / 原厂 GKI 内核 / vendor_boot 内 222 个模块 | `0xea759d7f` |
| 合并 `userdebug.config` 后本工程的内核与模块 | `0x9efa483d` |

**根因**：`userdebug.config` 改变了 `struct module` 布局。逐项比对 21 个相关
`CONFIG_*`（`MODULE_SIG`、`MODULE_UNLOAD`、`KALLSYMS`、`TRACEPOINTS`、
`CFI_CLANG`、`ARM64_PSEUDO_NMI` …）全部相同，最终靠"去掉 `userdebug.config`
做对照构建、CRC 精确回到 `0xea759d7f`"才锁定来源。

**修法**：改用 `mgk_debug_min.config`（只保留产出 `.ko` 的四个开关）。
详见 [BUILD.md](BUILD.md) 第 4 节。

---

## 7. boot 里的内核必须 LZ4 压缩

**现象**：刷 boot 后连 fastbootd 都进不去，只停在 bootloader。

**证据**：原厂 boot 内核段前 4 字节 `02 21 4c 18`（LZ4 帧头），
误刷版本为 `4d 5a 40 fa`（裸 `Image`），体积 39 MB vs 17 MB。

**根因**：引导器按 LZ4 解压，拿到裸 Image 直接失败。fastbootd 是用户态服务，
内核没起来就没有。

**修法**：用 `Image.lz4` 打包。打包后可用 `unpack_bootimg.py` 解包核对首 4 字节。

---

## 8. pstore 不保证跨冷启动保留

**现象**：崩溃后读 `/sys/fs/pstore/console-ramoops-0`，拿到的是一份**几周前**
的旧日志（内核版本号都对不上）。

**结论**：不能依赖事后取证。关键实验应当实时抓取，或用分级 `insmod` 在运行系统
上定位。本轮定位第 5 条问题时，正是靠"手动逐个 insmod 全部成功"才推翻了
"模块损坏"的假设。

---

## 9. wlan 的构建入口被改造

`connectivity/wlan/core/gen4m/` 下的 `Makefile` 与 `Kbuild` 是本工程重写的入口，
原始文件保留为 `Makefile.dispatch` / `Kbuild.dispatch`。厂商脚本依赖 ALPS 的
`$O/$M/DEVICE_MODULES_REL_DIR` 递归流程，与 Kleaf 的 `make -C <module_dir>` 不兼容。

另外 `wlan_drv_gen4m_6897.ko` 引用 `connadp.ko` 导出的 10 个符号，构建期不可见，
用 `KBUILD_MODPOST_WARN=1` 放行。**副作用**：该模块的 `depends` 只有 `cfg80211`，
`modules.dep` 里的依赖边不完整。当前依靠 `modules.load` 的顺序满足要求；
若出现真实的符号缺失，该开关会掩盖问题，需要逐项核对 `depends`。

---

## 10. 诊断流程（推荐顺序）

1. **先看三个属性**：`sys.boot_completed`、`init.svc.surfaceflinger`、
   `vendor.all.modules.ready`。第三个为 1 说明模块加载流程已走完，问题在下游。
2. **再数模块**：`wc -l < /proc/modules`，与上次可启动时对比。
3. **逐个对清单**：按 `modules.load` 顺序检查每个模块是否在 `/proc/modules`，
   判断是"前缀中断"还是"散开失败"——前者是 `modprobe` 提前退出，后者是依赖污染。
4. **对失败模块单独 `insmod`**：能成功即说明不是模块损坏，去查 `modules.dep`。
5. **看 `modules.dep` 的依赖行**：确认依赖里有没有"无法加载的模块"。
6. **最后才怀疑 ABI**：`grep -E 'Unknown symbol|protected symbol|duplicates|disagrees'`
   在 `dmesg` 里搜，这四类分别对应缺符号、签名、替代实现、CRC 不一致。
