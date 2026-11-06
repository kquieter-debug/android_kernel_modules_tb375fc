#!/bin/bash
# 从 out/kernel_modules 的扁平产物重建"仅含加载清单"的 vendor_dlkm 镜像。
#
# 用法:
#   bash common-modules/mediatek/tools/make_vendor_dlkm_trim.sh [输出镜像路径]
#
# 前置: 已执行
#   tools/bazel run //common-modules/mediatek:mgk_64_k61_dist
#
# 产物: out/kernel_modules/vendor_dlkm_trim.img（默认）
set -euo pipefail

PKG_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "${PKG_DIR}/../.." && pwd)
DIST="${ROOT}/out/kernel_modules"
LIST="${PKG_DIR}/vendor_dlkm_modules.load"
STAGE_BIN="${ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging"
TOOLS="${ROOT}/prebuilts/kernel-build-tools/linux-x86/bin"
OUT="${1:-${DIST}/vendor_dlkm_trim.img}"

for f in "${DIST}" "${LIST}" "${STAGE_BIN}" "${TOOLS}/mkfs.erofs" "${TOOLS}/depmod"; do
  [ -e "$f" ] || { echo "缺少: $f" >&2; exit 1; }
done

REL=$(ls "${STAGE_BIN}"/lib/modules | head -1)
echo "release      : ${REL}"
echo "modules list : $(wc -l < "${LIST}") 项"

WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT
STAGE="${WORK}/lib/modules/${REL}"
mkdir -p "${STAGE}"

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

# 沿用构建产物的内置模块信息
for f in modules.builtin modules.builtin.modinfo; do
  [ -f "${STAGE_BIN}/lib/modules/${REL}/${f}" ] && cp "${STAGE_BIN}/lib/modules/${REL}/${f}" "${STAGE}/"
done

# modules.order 与实际装载顺序一致
grep -v '^#' "${LIST}" | sed '/^$/d' > "${STAGE}/modules.order"

# 生成 modules.dep / .alias / .softdep
"${TOOLS}/depmod" -b "${WORK}" "${REL}"

# modules.load：开机加载清单（basename 形式，与原厂一致）
sed 's|.*/||' "${STAGE}/modules.order" > "${STAGE}/modules.load"

count=$(find "${STAGE}" -name '*.ko' | wc -l)
echo "staged modules: ${count}（缺失 ${missing}）"

rm -f "${OUT}"
# 原厂镜像为压缩 erofs（模块裸数据 29MB → 镜像 15MB），这里同样启用 lz4hc。
# 需要内核开启 CONFIG_EROFS_FS_ZIP，本设备原厂镜像即为压缩 erofs，可满足。
"${TOOLS}/mkfs.erofs" --quiet --all-root -zlz4hc,9 "${OUT}" "${WORK}" >/dev/null

echo "image        : ${OUT}"
ls -lh "${OUT}"
