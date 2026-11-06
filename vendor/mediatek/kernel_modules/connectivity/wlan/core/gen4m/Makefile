# Kleaf 入口驱动：递归进入 kbuild，由同目录的 Kbuild 固定 6897/SP 变体。
# 厂商原始的递归脚本保留在 Makefile.dispatch。
#
# KBUILD_MODPOST_WARN=1：本模块对 connadp.ko 导出的符号（conn_pwr_*、aee_*、
# register_file_buf_handler 等）在链接期不可见，运行期由 connadp.ko 提供
# （与原厂模块 depends=connadp 一致），因此允许 modpost 报警告而非失败。
CONN_DIR := $(abspath $(CURDIR)/../../..)
WLAN_CFLAGS := -I$(CONN_DIR)/conninfra/include \
               -I$(CONN_DIR)/conninfra/conn_drv/connv2 \
               -I$(CONN_DIR)/conninfra/conn_drv/connv2/debug_utility \
               -I$(CONN_DIR)/conninfra/conn_drv/connv2/debug_utility/include \
               -I$(CONN_DIR)/conninfra/conn_drv/connv2/debug_utility/metlog \
               -I$(CONN_DIR)/conninfra/conn_drv/connv3/debug_utility/connsyslog \
               -I$(CONN_DIR)/conninfra/conn_drv/connv3/debug_utility/include \
               -I$(CONN_DIR)/connfem/include \
               -I$(CONN_DIR)/common \
               -I$(CONN_DIR)/common/include \
               -I$(CONN_DIR)/common/common_main/include \
               -I$(CONN_DIR)/common/debug_utility

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS) \
		EXTRA_CFLAGS="$(WLAN_CFLAGS)" \
		KBUILD_MODPOST_WARN=1 \
		MODULE_NAME=wlan_drv_gen4m_6897 SEGMENT=SP

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
