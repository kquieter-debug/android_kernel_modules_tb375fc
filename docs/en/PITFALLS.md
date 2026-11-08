# Pitfalls

Every item has been verified on hardware, in a uniform format:
symptom → evidence → root cause → fix. Items are ordered by the priority of
"will leave the device stuck at the boot logo".

---

## 1. Module directory must use a flat layout

**Symptom**: after flashing, the device is stuck at the logo; `vendor_dlkm` is
mounted and SELinux labels are correct, but `/proc/modules` contains no vendor
module at all.

**Evidence**: on the device, `/vendor_dlkm/lib/modules/` contains only one
subdirectory, `6.1.138-android14-11-maybe-dirty/`.

**Root cause**: `init` reads `/vendor_dlkm/lib/modules/modules.load` directly
and does not look inside version subdirectories. Kleaf `kernel_modules_install`
defaults to `lib/modules/<release>/`, which violates the partition contract.

**Fix**: stage all `.ko` files flat into `lib/modules/`; still run `depmod` in a
temporary version directory, then rewrite the paths in `modules.dep` to the
absolute form `/vendor_dlkm/lib/modules/x.ko`.

---

## 2. The image must carry SELinux labels

**Symptom**: the device is stuck at the logo and `init.insmod.sh` fails to load
modules.

**Evidence**:

```
avc: denied { search } for comm="init.insmod.sh" name="/" dev="dm-7"
     scontext=u:r:init_insmod_sh:s0 tcontext=u:object_r:unlabeled:s0 tclass=dir
```

`dm-7` is `/vendor_dlkm`, whose root directory is `unlabeled`.

**Root cause**: `mkfs.erofs` was missing `--mount-point` / `--fs-config-file` /
`--file-contexts`, so inodes have no `security.selinux` extended attribute.

**Fix**: add all three options, and generate contexts and the permission table
per path from the actual contents (modules use `vendor_file`, files under
`etc/` use `vendor_configs_file`). The stock static list enumerates files under
the original kernel release directory name, so any sublevel change breaks it;
it cannot be reused directly.

---

## 3. `modules.load` must keep the stock order

**Symptom**: after adding a batch of "symbol provider" modules, the device
**crashes and reboots** after the logo.

**Evidence**: comparison showed the list grew from 187 to 274 entries, of which
89 are modules not loaded by the stock image; triggering occurred after
replacing the order with a symbol-dependency topological sort.

**Root cause**: the order itself encodes the hardware power-up sequence
(clock → pinctrl → PMIC → regulator → power domain → display). `modprobe -a`
loads everything at once; after reordering, probes in the early boot stage run
out of order and the boot flow cannot complete (this is not a panic — the flow
simply never finishes).

**Fix**: `modules.load` = stock `modules.load.stock` (187) + necessary additions
from `modules.load.extra` (5), with the insertion point fixed before
`connadp.ko`. `tools/solve_deps.py` is for offline diagnostics only; **do not**
use its output as a load list.

---

## 4. Modules must be signed

**Symptom**: `insmod` of individual modules returns `Permission denied`, while
other modules work normally.

**Evidence**: kernel `kernel/module/main.c`:

```c
if (!mod->sig_ok && gki_is_module_protected_export(sym)) {
        pr_err("%s: exports protected symbol %s\n", ...);
        return -EACCES;
}
```

Hits: `libarc4` (`arc4_setkey`/`arc4_crypt`), `mii` (`generic_mii_ioctl`); the
latter cascades through `usbnet` → `smsc75xx/95xx` and breaks the whole chain.

**Root cause**: all modules are unsigned and `sig_ok` is false.

**Two wrong paths (do not retry)**:

| Attempt | Result |
|---|---|
| `CONFIG_CRYPTO_LIB_ARC4=y` | No effect; the `select` in `net/mac80211/Kconfig` pins it to `=m` |
| `CONFIG_MODULE_SIG_ALL=y` | Build fails: `Makefile.modinst` needs `certs/signing_key.x509`, which the Kleaf sandbox generates but does not export |

**Fix**: at packaging time, `tools/sign_modules.sh` signs each module with
`sign-file sha1` using the same `certs/signing_key.pem/x509` as the kernel
build. Because the key matches, `sig_ok` becomes true.

---

## 5. Do not package "alternative implementation" modules (they poison `modules.dep`)

**Symptom**: the device is stuck at the logo, `init.svc.surfaceflinger=restarting`,
and SurfaceFlinger crashes in `SkiaGLRenderEngine::chooseEglConfig` (EGL cannot
get a config) while `/dev/mali0` does not exist. The failing modules are
scattered, not "everything fails from a certain point on".

**Evidence**: manually `insmod`ing `mali_kbase`, `ged`, `gz_main_mod`,
`tmem_ffa`, and `mtk_sec_heap` one by one gives **rc=0 for all** — the modules
themselves are fine.

In `modules.dep`:

```
mali_kbase_mt6897_r44.ko: ged.ko … tmem_ffa.ko ffa_v10.ko mcDrvModule.ko …
```

and `ffa_v10` / `mcDrvModule` fail to load due to duplicate symbols:

```
ffa_v10:     exports duplicate symbol ffa_bus_type (owned by ffa_module)
mcDrvModule: exports duplicate symbol mc_close_device (owned by mcDrvModule_ffa)
```

**Root cause**: this tree produces two implementations for each of three
subsystems, while the device actually runs the set from `vendor_boot`. `depmod`
records this tree's outputs in other modules' `depends`; `modprobe` must satisfy
dependencies first, so it pulls in a module that is bound to fail, and the whole
chain fails.

| Produced by this tree | Actually used by the device | Affected downstream |
|---|---|---|
| `ffa_v10.ko` | `ffa_module.ko` | mali_kbase, gz_main_mod, tmem_ffa, mtk_sec_heap, mtk_disp_sec, widevine_driver, sapu, t-base-tui, mtk_heap_debug, mtk-cam-isp7sp |
| `mcDrvModule.ko` | `mcDrvModule_ffa.ko` | same as above |
| `scpsys-dummy.ko` | `mtk_scpsys*.ko` | load failure (ENOEXEC) |

**Fix**: remove these three from `image_contents.load`. After removal,
`modules.dep` no longer references them, and undefined symbols are resolved
directly at `insmod` time by the already-loaded `vendor_boot` modules.

**General criterion**: any module that "is produced by this tree but already has
another implementation of the same function on the device" should not enter the
image. How to check: `insmod` it on a running system; `exports duplicate symbol`
or `File exists` means it belongs to this category.

---

## 6. `module_layout` CRC must match the stock one

**Symptom**: module `insmod` reports `Exec format error`.

**Evidence**:

```
mtk_lpm: disagrees about version of symbol module_layout
```

| Source | CRC |
|---|---|
| Stock vendor modules / stock GKI kernel / the 222 modules in `vendor_boot` | `0xea759d7f` |
| This project's kernel and modules after merging `userdebug.config` | `0x9efa483d` |

**Root cause**: `userdebug.config` changes the layout of `struct module`.
Item-by-item comparison of 21 related `CONFIG_*` options (`MODULE_SIG`,
`MODULE_UNLOAD`, `KALLSYMS`, `TRACEPOINTS`, `CFI_CLANG`, `ARM64_PSEUDO_NMI`, …)
showed all were identical; the source was finally pinned down with a control
build that dropped `userdebug.config`, which restored the CRC exactly to
`0xea759d7f`.

**Fix**: switch to `mgk_debug_min.config` (keeping only the four switches that
produce `.ko` files). See section 4 of [BUILD.md](BUILD.md).

---

## 7. The kernel in boot must be LZ4-compressed

**Symptom**: after flashing boot, even fastbootd is unreachable; the device
stops at the bootloader.

**Evidence**: the first 4 bytes of the stock boot kernel section are
`02 21 4c 18` (LZ4 frame header); the wrongly flashed version has
`4d 5a 40 fa` (raw `Image`) and is 39 MB instead of 17 MB.

**Root cause**: the bootloader decompresses with LZ4 and fails immediately when
given a raw Image. fastbootd is a userspace service; without a running kernel,
there is no fastbootd.

**Fix**: package with `Image.lz4`. After packaging, use `unpack_bootimg.py` to
unpack and verify the first 4 bytes.

---

## 8. pstore is not guaranteed to survive cold boots

**Symptom**: after a crash, reading `/sys/fs/pstore/console-ramoops-0` returns an
old log from **weeks ago** (even the kernel version does not match).

**Conclusion**: do not rely on post-mortem forensics. Critical experiments should
be captured in real time, or located on a running system with staged `insmod`.
When investigating item 5 above, it was exactly "manual insmod of every module
succeeds" that overturned the "modules are broken" hypothesis.

---

## 9. The wlan build entry was reworked

The `Makefile` and `Kbuild` under `connectivity/wlan/core/gen4m/` are rewritten
entry points for this project; the original files are kept as
`Makefile.dispatch` / `Kbuild.dispatch`. The vendor scripts rely on the ALPS
`$O/$M/DEVICE_MODULES_REL_DIR` recursive flow, which is incompatible with
Kleaf's `make -C <module_dir>`.

In addition, `wlan_drv_gen4m_6897.ko` references 10 symbols exported by
`connadp.ko`, which are not visible at build time, so they are allowed via
`KBUILD_MODPOST_WARN=1`. **Side effect**: that module's `depends` contains only
`cfg80211`, so the dependency edge in `modules.dep` is incomplete. Currently the
`modules.load` order satisfies the requirement; if a real symbol is missing,
this switch masks the problem, so `depends` must be checked item by item.

---

## 10. Diagnostic flow (recommended order)

1. **Check the three properties first**: `sys.boot_completed`,
   `init.svc.surfaceflinger`, `vendor.all.modules.ready`. If the third is 1,
   the module loading flow has completed and the problem is downstream.
2. **Count modules**: `wc -l < /proc/modules`, and compare with the last
   bootable state.
3. **Check each entry against the list**: in `modules.load` order, check whether
   each module appears in `/proc/modules`, and decide whether it is a "prefix
   interruption" or a "scattered failure" — the former means `modprobe` exited
   early, the latter means dependency poisoning.
4. **`insmod` a failing module individually**: if it succeeds, the module is not
   broken; go and inspect `modules.dep`.
5. **Look at dependency lines in `modules.dep`**: confirm whether any dependency
   is a "module that cannot load".
6. **Suspect ABI last**: search `dmesg` with
   `grep -E 'Unknown symbol|protected symbol|duplicates|disagrees'`; these four
   classes correspond to a missing symbol, signature, alternative
   implementation, and CRC mismatch, respectively.
