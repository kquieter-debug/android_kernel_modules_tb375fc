load("//build/kernel/kleaf:kernel.bzl", "kernel_module", "kernel_module_group")

_DEVICE_MODULES = "//common-modules/mediatek/kernel_device_modules-6.1:mgk_device_modules"
_KERNEL_BUILD = "//common-modules/mediatek:mgk_64_k61"

def define_mgk_ko(name, srcs = None, outs = None, deps = None, makefile = None):
    if srcs == None:
        srcs = native.glob(["**/*.c", "**/*.h", "**/Kbuild", "**/Makefile"], exclude = [".*", ".*/**"])
    if outs == None:
        outs = [name + ".ko"]
    module_deps = [] if deps == None else deps
    if module_deps:
        kernel_module_group(name = name + "_deps", srcs = module_deps + [_DEVICE_MODULES])
        module_deps = [":" + name + "_deps"]
    else:
        module_deps = [_DEVICE_MODULES]
    kernel_module(
        name = name,
        srcs = srcs,
        outs = outs,
        kernel_build = _KERNEL_BUILD,
        deps = module_deps,
        makefile = makefile,
    )
