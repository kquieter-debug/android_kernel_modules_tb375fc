#!/bin/bash
# 从 out/kernel_modules 的扁平产物重建 vendor_dlkm 镜像。
#
# 用法:
#   bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh [输出镜像路径]
#
# 前置: 已执行
#   tools/bazel run //common-modules/mediatek:mgk_64_k61_dist
#
# 产物: out/kernel_modules/vendor_dlkm_trim.img（默认）
#
# 三个必须遵守的约束（均为实机踩坑得出）：
#  1. 目录布局必须**扁平**：模块直接放在 lib/modules/ 下，modules.load 也在该层。
#     放入 lib/modules/<release>/ 会导致 init 找不到 modules.load，模块一个都不加载。
#  2. modules.dep 使用绝对路径 /vendor_dlkm/lib/modules/xxx.ko。
#  3. 必须带 --mount-point / --fs-config-file / --file-contexts，否则文件无 SELinux
#     标签，init.insmod.sh 会被 avc denied 拒绝。
set -euo pipefail

PKG_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "${PKG_DIR}/../.." && pwd)
DIST="${ROOT}/out/kernel_modules"
CFG_DIR="${PKG_DIR}/tools/vendor_dlkm"
# 镜像内容：原厂镜像里的全部模块中本树能构建的部分
LIST="${CFG_DIR}/image_contents.load"
# 开机自动加载清单：严格沿用原厂 modules.load 的顺序。
# 顺序本身编码了硬件上电次序（时钟→pinctrl→PMIC→regulator→power domain→display），
# 不能替换成按符号依赖排出来的顺序，否则早启动阶段探测会失序。
LOADLIST="${CFG_DIR}/modules.load.stock"
# 需要在 anchor 之前补入的符号提供者（本树把这些编成独立模块，原厂是内建）。
# 只补真正缺少的：CCCI 簇提供 get_smem_phy_start_addr / exec_ccci_kern_func 等，
# conninfra 及其上面的 WiFi/BT/FM 依赖它们。
EXTRA="${CFG_DIR}/modules.load.extra"
ANCHOR=connadp.ko
STAGE_BIN="${ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging"
TOOLS="${ROOT}/prebuilts/kernel-build-tools/linux-x86/bin"
OUT="${1:-${DIST}/vendor_dlkm_trim.img}"
MODDIR=/vendor_dlkm/lib/modules

for f in "${DIST}" "${LIST}" "${LOADLIST}" "${EXTRA}" "${STAGE_BIN}" \
         "${TOOLS}/mkfs.erofs" "${TOOLS}/depmod" "${CFG_DIR}/build.prop"; do
  [ -e "$f" ] || { echo "缺少: $f" >&2; exit 1; }
done

REL=$(ls "${STAGE_BIN}"/lib/modules | head -1)
echo "build release: ${REL}"
echo "modules list : $(wc -l < "${LIST}") 项"

WORK=$(mktemp -d)
FC=$(mktemp)
FSCFG=$(mktemp)
DEPMOD_ROOT=$(mktemp -d)
trap 'rm -rf "${WORK}" "${DEPMOD_ROOT}" "${FC}" "${FSCFG}"' EXIT

FLAT="${WORK}/lib/modules"
mkdir -p "${FLAT}" "${WORK}/etc"

missing=0
while IFS= read -r p; do
  [ -n "$p" ] || continue
  base=${p##*/}
  src="${DIST}/${base}"
  if [ ! -f "${src}" ]; then
    echo "  ! 缺少产物: ${base}" >&2
    missing=$((missing + 1))
    continue
  fi
  cp "${src}" "${FLAT}/${base}"
done < "${LIST}"

# 与原厂镜像一致：携带 etc/build.prop
cp "${CFG_DIR}/build.prop" "${WORK}/etc/build.prop"

count=$(find "${FLAT}" -name '*.ko' | wc -l)
echo "staged modules: ${count}（缺失 ${missing}）"

# 模块签名：未签名模块导出 GKI 保护符号会被内核拒绝（EACCES）
bash "${PKG_DIR}/tools/sign_modules.sh" "${FLAT}"

# depmod 需要版本目录，在临时根里生成后再把路径改写成扁平的绝对路径
mkdir -p "${DEPMOD_ROOT}/lib/modules/${REL}"
cp "${FLAT}"/*.ko "${DEPMOD_ROOT}/lib/modules/${REL}/"
for f in modules.order modules.builtin modules.builtin.modinfo; do
  [ -f "${STAGE_BIN}/lib/modules/${REL}/${f}" ] && \
    cp "${STAGE_BIN}/lib/modules/${REL}/${f}" "${DEPMOD_ROOT}/lib/modules/${REL}/"
done
"${TOOLS}/depmod" -b "${DEPMOD_ROOT}" "${REL}"

rewrite() {
  awk -v moddir="${MODDIR}" -F': ' '{
    target = $1
    sub(/:$/, "", target)
    sub(/.*\//, "", target)
    line = moddir "/" target ":"
    if (NF > 1) {
      n = split($2, deps, " ")
      for (i = 1; i <= n; i++) {
        d = deps[i]
        if (d == "") continue
        sub(/.*\//, "", d)
        line = line " " moddir "/" d
      }
    }
    print line
  }'
}
rewrite < "${DEPMOD_ROOT}/lib/modules/${REL}/modules.dep" > "${FLAT}/modules.dep"

for f in modules.alias modules.softdep modules.builtin modules.builtin.modinfo; do
  [ -f "${DEPMOD_ROOT}/lib/modules/${REL}/${f}" ] && \
    cp "${DEPMOD_ROOT}/lib/modules/${REL}/${f}" "${FLAT}/${f}"
done

# modules.order：与镜像内容一致
find "${FLAT}" -name '*.ko' -printf '%f\n' | LC_ALL=C sort > "${FLAT}/modules.order"

# 合并加载清单：原厂顺序 + 在 anchor 处插入 EXTRA
TARGET=$(mktemp)
awk -v anchor="${ANCHOR}" -v extra="${EXTRA}" '
  BEGIN {
    n = 0
    while ((getline line < extra) > 0) {
      gsub(/\r/, "", line)
      if (line != "") ins[++n] = line
    }
    close(extra)
    done = 0
  }
  {
    if (!done && $0 == anchor) {
      for (i = 1; i <= n; i++) print ins[i]
      done = 1
    }
    print
  }
' "${LOADLIST}" > "${TARGET}"

# modules.load：按合并后的清单，仅保留镜像内实际存在的模块
: > "${FLAT}/modules.load"
while IFS= read -r m; do
  [ -n "$m" ] || continue
  [ -f "${FLAT}/${m}" ] && echo "${m}" >> "${FLAT}/modules.load"
done < "${TARGET}"
rm -f "${TARGET}"

# SELinux 上下文：模块 vendor_file，etc 下 vendor_configs_file
{
  echo "/ u:object_r:vendor_file:s0"
  echo "/vendor_dlkm u:object_r:vendor_file:s0"
  find "${WORK}" -mindepth 1 | sed "s|^${WORK}||" | LC_ALL=C sort | while IFS= read -r p; do
    esc=$(printf '%s' "/vendor_dlkm${p}" | sed 's/\./\\./g')
    case "$p" in
      /etc|/etc/*) echo "${esc} u:object_r:vendor_configs_file:s0" ;;
      *)           echo "${esc} u:object_r:vendor_file:s0" ;;
    esac
  done
} > "${FC}"

# fs_config：目录 0755、文件 0644，路径前缀受 --mount-point 影响
{
  echo "/ 0 0 0755"
  echo "vendor_dlkm 0 0 0755"
  find "${WORK}" -mindepth 1 | sed "s|^${WORK}/||" | LC_ALL=C sort | while IFS= read -r p; do
    if [ -d "${WORK}/${p}" ]; then
      echo "vendor_dlkm/${p} 0 0 0755"
    else
      echo "vendor_dlkm/${p} 0 0 0644"
    fi
  done
} > "${FSCFG}"

rm -f "${OUT}"
"${TOOLS}/mkfs.erofs" --quiet \
  -zlz4hc -T 0 --all-root \
  --mount-point=/vendor_dlkm \
  --fs-config-file="${FSCFG}" \
  --file-contexts="${FC}" \
  "${OUT}" "${WORK}" >/dev/null

echo "image        : ${OUT}"
ls -lh "${OUT}"
