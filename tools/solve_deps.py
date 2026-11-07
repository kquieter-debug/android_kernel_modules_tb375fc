#!/usr/bin/env python3
"""计算 vendor_dlkm 需要包含的完整模块集合，并给出依赖排序。

输入：Kleaf staging 目录（全部 .ko）+ 内核导出符号表 + 原厂镜像清单
输出：image_contents.load（全路径清单）、modules.load.final（按依赖排序的 basename）
"""
import os
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
NM = f"{ROOT}/prebuilts/clang/host/linux-x86/clang-r487747c/bin/llvm-nm"
STAGE = f"{ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_modules_install/staging/lib/modules"
SYMVERS = f"{ROOT}/bazel-bin/common-modules/mediatek/mgk_64_k61_kernel_aarch64/vmlinux.symvers"
CFG = f"{ROOT}/common-modules/mediatek/tools/vendor_dlkm"
STOCK_LIST = f"{CFG}/image_contents.load"
STOCK_LOAD = f"{CFG}/modules.load.stock"


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout


def main():
    rel = os.listdir(STAGE)[0]
    base = f"{STAGE}/{rel}"

    # 1) staging 下所有 .ko 的路径、导出符号、未定义符号
    paths = {}
    defines = defaultdict(set)
    undef = defaultdict(set)
    out = sh(f"find {base} -name '*.ko'")
    for line in out.split():
        name = os.path.basename(line)
        paths[name] = line.replace(base + "/", "")
        d = sh(f"{NM} -g --defined-only {line} | awk '{{print $NF}}'")
        defines[name] = set(d.split())
        u = sh(f"{NM} -u {line} | awk '{{print $NF}}'")
        undef[name] = set(u.split())

    kern = set()
    with open(SYMVERS) as fh:
        for line in fh:
            parts = line.split("\t")
            if len(parts) > 1:
                kern.add(parts[1].strip())

    provider = defaultdict(set)
    for mod, syms in defines.items():
        for s in syms:
            provider[s].add(mod)

    # 2) 以原厂清单为起点，按缺口补入提供者
    start = []
    with open(STOCK_LIST) as fh:
        for line in fh:
            line = line.strip()
            if line:
                start.append(os.path.basename(line))

    selected = [m for m in start if m in paths]
    added = []
    for _ in range(10):
        have = set()
        for m in selected:
            have |= defines[m]
        need = set()
        for m in selected:
            need |= (undef[m] - kern - have)
        unresolved = need - kern - have
        if not unresolved:
            break
        new = set()
        for s in unresolved:
            cands = provider.get(s, set()) - set(selected)
            if cands:
                # 选依赖最少的提供者
                new.add(sorted(cands, key=lambda x: len(undef[x]))[0])
        if not new:
            print("仍无法满足的符号数:", len(unresolved))
            for s in sorted(unresolved)[:10]:
                print("   ", s)
            break
        selected.extend(sorted(new))
        added.extend(sorted(new))

    # 3) 依赖拓扑排序（模块 A 依赖 B 表示 A 需要 B 导出的符号）
    sel = set(selected)

    def deps(m):
        out = set()
        for s in undef[m]:
            for p in provider.get(s, ()):
                if p in sel and p != m:
                    out.add(p)
        return out

    order, state = [], {}

    def visit(m, stack=()):
        if state.get(m) == 2:
            return
        if state.get(m) == 1:
            return
        state[m] = 1
        for d in sorted(deps(m)):
            visit(d, stack + (m,))
        state[m] = 2
        order.append(m)

    # 以原厂顺序为骨架，其余模块按依赖插入
    for m in selected:
        visit(m)

    print(f"原厂清单模块: {len(start)}")
    print(f"补入提供者  : {len(added)}")
    for m in added:
        print("   +", m)
    print(f"最终模块数  : {len(order)}")

    with open(f"{CFG}/image_contents.load", "w") as fh:
        for m in order:
            fh.write(paths[m] + "\n")
    with open(f"{CFG}/modules.load.final", "w") as fh:
        for m in order:
            fh.write(m + "\n")
    print("已写出 image_contents.load 与 modules.load.final")


if __name__ == "__main__":
    main()
