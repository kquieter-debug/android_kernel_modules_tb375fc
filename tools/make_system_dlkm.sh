#!/bin/bash
# 从 Kleaf staging 产出可刷入的 system_dlkm 镜像（扁平布局，erofs）。
#
# 用法:
#   bash common-modules/mediatek/tools/make_system_dlkm.sh [输出镜像路径]
#
# 前置:
#   tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install
#
# 产物: out/kernel_modules/system_dlkm.img（默认）
#
# 与 vendor_dlkm 同源的三个约束：
#  1. 扁平布局：模块直接放 lib/modules/，modules.load 同层；
#  2. modules.dep 用绝对路径 /system_dlkm/lib/modules/xxx.ko；
#  3. mkfs.erofs 必须带 --mount-point / --fs-config-file / --file-contexts，
#     system_dlkm 内文件的 SELinux 类型是 system_file。
set -euo pipefail

PKG_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "${PKG_DIR}/../.." && pwd)
CFG_DIR="${PKG_DIR}/tools/system_dlkm"
LOADLIST="${CFG_DIR}/modules.load.stock"
DIST="${ROOT}/out/kernel_modules"
STAGE_BIN="${ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging"
TOOLS="${ROOT}/prebuilts/kernel-build-tools/linux-x86/bin"
OUT="${1:-${DIST}/system_dlkm.img}"
MODDIR=/system_dlkm/lib/modules

for f in "${LOADLIST}" "${STAGE_BIN}" "${TOOLS}/mkfs.erofs" "${TOOLS}/depmod" "${CFG_DIR}/build.prop"; do
  [ -e "$f" ] || { echo "缺少: $f" >&2; exit 1; }
done

REL=$(ls "${STAGE_BIN}"/lib/modules | head -1)
echo "build release: ${REL}"
echo "load list    : $(wc -l < "${LOADLIST}") 项"

WORK=$(mktemp -d)
FC=$(mktemp)
FSCFG=$(mktemp)
DEPMOD_ROOT=$(mktemp -d)
trap 'rm -rf "${WORK}" "${DEPMOD_ROOT}" "${FC}" "${FSCFG}"' EXIT

FLAT="${WORK}/lib/modules"
mkdir -p "${FLAT}" "${WORK}/etc"
cp "${CFG_DIR}/build.prop" "${WORK}/etc/build.prop"

missing=0
while IFS= read -r m; do
  [ -n "$m" ] || continue
  src=$(find "${STAGE_BIN}/lib/modules/${REL}/kernel" -name "${m}" -print -quit 2>/dev/null || true)
  if [ -z "${src}" ]; then
    echo "  ! staging 中找不到: ${m}" >&2
    missing=$((missing + 1))
    continue
  fi
  cp "${src}" "${FLAT}/${m}"
done < "${LOADLIST}"

count=$(find "${FLAT}" -name '*.ko' | wc -l)
echo "staged modules: ${count}（缺失 ${missing}）"

# 模块签名：未签名模块导出 GKI 保护符号会被内核拒绝（EACCES）
bash "${PKG_DIR}/tools/sign_modules.sh" "${FLAT}"

# depmod 需要版本目录，生成后把路径改写成扁平绝对路径
mkdir -p "${DEPMOD_ROOT}/lib/modules/${REL}"
cp "${FLAT}"/*.ko "${DEPMOD_ROOT}/lib/modules/${REL}/"
for f in modules.order modules.builtin modules.builtin.modinfo; do
  [ -f "${STAGE_BIN}/lib/modules/${REL}/${f}" ] && \
    cp "${STAGE_BIN}/lib/modules/${REL}/${f}" "${DEPMOD_ROOT}/lib/modules/${REL}/"
done
"${TOOLS}/depmod" -b "${DEPMOD_ROOT}" "${REL}"

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
}' < "${DEPMOD_ROOT}/lib/modules/${REL}/modules.dep" > "${FLAT}/modules.dep"

for f in modules.alias modules.softdep modules.builtin modules.builtin.modinfo; do
  [ -f "${DEPMOD_ROOT}/lib/modules/${REL}/${f}" ] && \
    cp "${DEPMOD_ROOT}/lib/modules/${REL}/${f}" "${FLAT}/${f}"
done

find "${FLAT}" -name '*.ko' -printf '%f\n' | LC_ALL=C sort > "${FLAT}/modules.order"

# modules.load：沿用原厂顺序，仅保留实际存在的模块
: > "${FLAT}/modules.load"
while IFS= read -r m; do
  [ -n "$m" ] || continue
  [ -f "${FLAT}/${m}" ] && echo "${m}" >> "${FLAT}/modules.load"
done < "${LOADLIST}"

# SELinux：system_dlkm 内的文件类型是 system_file
{
  echo "/ u:object_r:system_file:s0"
  echo "/system_dlkm u:object_r:system_file:s0"
  find "${WORK}" -mindepth 1 | sed "s|^${WORK}||" | LC_ALL=C sort | while IFS= read -r p; do
    esc=$(printf '%s' "/system_dlkm${p}" | sed 's/\./\\./g')
    case "$p" in
      /etc|/etc/*) echo "${esc} u:object_r:system_file:s0" ;;
      *)           echo "${esc} u:object_r:system_file:s0" ;;
    esac
  done
} > "${FC}"

{
  echo "/ 0 0 0755"
  echo "system_dlkm 0 0 0755"
  find "${WORK}" -mindepth 1 | sed "s|^${WORK}/||" | LC_ALL=C sort | while IFS= read -r p; do
    if [ -d "${WORK}/${p}" ]; then
      echo "system_dlkm/${p} 0 0 0755"
    else
      echo "system_dlkm/${p} 0 0 0644"
    fi
  done
} > "${FSCFG}"

rm -f "${OUT}"
"${TOOLS}/mkfs.erofs" --quiet \
  -zlz4hc -T 0 --all-root \
  --mount-point=/system_dlkm \
  --fs-config-file="${FSCFG}" \
  --file-contexts="${FC}" \
  "${OUT}" "${WORK}" >/dev/null

echo "image        : ${OUT}"
ls -lh "${OUT}"
