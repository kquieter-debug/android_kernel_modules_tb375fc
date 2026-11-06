#!/bin/bash
# 从 out/kernel_modules 的扁平产物重建 vendor_dlkm 镜像（仅含加载清单内的模块）。
#
# 用法:
#   bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh [输出镜像路径]
#
# 前置: 已执行
#   tools/bazel run //common-modules/mediatek:mgk_64_k61_dist
#
# 产物: out/kernel_modules/vendor_dlkm_trim.img（默认）
#
# 重要：必须带 --mount-point / --fs-config-file / --file-contexts，
# 否则镜像内文件没有 SELinux 标签（u:object_r:unlabeled:s0），
# init.insmod.sh 会被 avc denied 拒绝进入 /vendor_dlkm，模块全部加载失败。
set -euo pipefail

PKG_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "${PKG_DIR}/../.." && pwd)
DIST="${ROOT}/out/kernel_modules"
CFG_DIR="${PKG_DIR}/tools/vendor_dlkm"
# 镜像内容：原厂镜像里的全部模块中本树能构建的部分（204 个）
LIST="${CFG_DIR}/image_contents.load"
# 开机自动加载清单：沿用原厂 modules.load（187 项）
LOADLIST="${CFG_DIR}/modules.load.stock"
STAGE_BIN="${ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging"
TOOLS="${ROOT}/prebuilts/kernel-build-tools/linux-x86/bin"
OUT="${1:-${DIST}/vendor_dlkm_trim.img}"

for f in "${DIST}" "${LIST}" "${LOADLIST}" "${STAGE_BIN}" "${TOOLS}/mkfs.erofs" "${TOOLS}/depmod" \
         "${CFG_DIR}/build.prop"; do
  [ -e "$f" ] || { echo "缺少: $f" >&2; exit 1; }
done

REL=$(ls "${STAGE_BIN}"/lib/modules | head -1)
echo "release      : ${REL}"
echo "modules list : $(wc -l < "${LIST}") 项"

WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT
STAGE="${WORK}/lib/modules/${REL}"
mkdir -p "${STAGE}" "${WORK}/etc"

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
  mkdir -p "${STAGE}/$(dirname "${p}")"
  cp "${src}" "${STAGE}/${p}"
done < "${LIST}"

# 与原厂镜像一致：携带 etc/build.prop
cp "${CFG_DIR}/build.prop" "${WORK}/etc/build.prop"

for f in modules.builtin modules.builtin.modinfo; do
  [ -f "${STAGE_BIN}/lib/modules/${REL}/${f}" ] && cp "${STAGE_BIN}/lib/modules/${REL}/${f}" "${STAGE}/"
done

# modules.order 与实际装载顺序一致
grep -v '^#' "${LIST}" | sed '/^$/d' > "${STAGE}/modules.order"

# 生成 modules.dep / .alias / .softdep
"${TOOLS}/depmod" -b "${WORK}" "${REL}"

# modules.load：沿用原厂加载顺序，仅保留镜像内实际存在的模块
: > "${STAGE}/modules.load"
while IFS= read -r m; do
  [ -n "$m" ] || continue
  if [ -n "$(find "${STAGE}" -name "${m}" -print -quit)" ]; then
    echo "${m}" >> "${STAGE}/modules.load"
  fi
done < "${LOADLIST}"

count=$(find "${STAGE}" -name '*.ko' | wc -l)
echo "staged modules: ${count}（缺失 ${missing}）"

# 按实际内容生成 SELinux 上下文：模块一律 vendor_file，etc 下为 vendor_configs_file
# 注意：上下文文件必须放在镜像目录之外，否则会被当成镜像内容且 fs_config 里没有对应条目。
FC=$(mktemp)
# fs_config 同样按实际内容生成：目录 0755、文件 0644，root:root。
# 原厂 fs_config 是逐路径列举的，内核 sublevel 变化后目录名不同，直接复用会报错。
FSCFG=$(mktemp)
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

rm -f "${OUT}"
# 参数与原厂镜像保持一致：lz4hc、固定时间戳、挂载点、权限与 SELinux 标签
"${TOOLS}/mkfs.erofs" --quiet \
  -zlz4hc -T 0 --all-root \
  --mount-point=/vendor_dlkm \
  --fs-config-file="${FSCFG}" \
  --file-contexts="${FC}" \
  "${OUT}" "${WORK}" >/dev/null

echo "image        : ${OUT}"
ls -lh "${OUT}"
