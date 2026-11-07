#!/bin/bash
# 给目录下的所有 .ko 追加模块签名。
#
# 背景：kernel/module/main.c 的 verify_exported_symbols() 中
#   if (!mod->sig_ok && gki_is_module_protected_export(sym)) return -EACCES;
# 未签名模块只要导出了 GKI 保护符号就会被拒绝加载（insmod 报 Permission denied）。
# 已确认命中：libarc4（arc4_setkey/arc4_crypt）、mii（generic_mii_ioctl）。
#
# 签名密钥与内核构建时 CONFIG_MODULE_SIG_KEY 指向的是同一份
# （//common:kernel_aarch64/certs/signing_key.pem），因此内核的
# builtin_trusted_keys 里已有对应证书，签出的模块 sig_ok 为真。
#
# 用法: bash tools/sign_modules.sh <包含 .ko 的目录>
set -euo pipefail

DIR=${1:?用法: sign_modules.sh <包含 .ko 的目录>}
PKG_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "${PKG_DIR}/../.." && pwd)
SIGNER="${PKG_DIR}/tools/bin/sign-file"
KEY="${ROOT}/bazel-bin/common/kernel_aarch64/certs/signing_key.pem"
CERT="${ROOT}/bazel-bin/common/kernel_aarch64/certs/signing_key.x509"

[ -d "${DIR}" ] || { echo "目录不存在: ${DIR}" >&2; exit 1; }

# sign-file 是内核脚本，首次使用时本地编译
if [ ! -x "${SIGNER}" ]; then
  command -v gcc >/dev/null 2>&1 || { echo "缺少 gcc，无法编译 sign-file" >&2; exit 1; }
  mkdir -p "$(dirname "${SIGNER}")"
  gcc -O2 -o "${SIGNER}" "${ROOT}/common/scripts/sign-file.c" -lcrypto
  echo "已编译 sign-file"
fi

for f in "${KEY}" "${CERT}"; do
  [ -f "$f" ] || { echo "缺少签名密钥/证书: $f" >&2; exit 1; }
done

n=0
while IFS= read -r -d '' ko; do
  "${SIGNER}" sha1 "${KEY}" "${CERT}" "${ko}"
  n=$((n + 1))
done < <(find "${DIR}" -name '*.ko' -print0)

echo "signed: ${n} modules"
