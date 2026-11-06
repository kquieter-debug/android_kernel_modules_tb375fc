LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := extend_reclaim.ko


ifeq ($(strip $(LGSI_PLATFORM)),qcom)
    DLKM_DIR := $(TOP)/device/qcom/common/dlkm
    LOCAL_MODULE_KBUILD_NAME := extend_reclaim.ko
    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
    KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki
    KERNEL_DIR := $(LGSI_ROOT)/../../kernel_platform
    ifneq ($(wildcard $(KERNEL_DIR)), )
        include $(DLKM_DIR)/Build_external_kernelmodule.mk
    else
        KERNEL_DIR := $(LGSI_DEVICE_COMMON)/../../../Linux/android/kernel_platform
        ifneq ($(wildcard $(KERNEL_DIR)), )
            include $(DLKM_DIR)/Build_external_kernelmodule.mk
        else
            $(warning "we are not in vendor project")
        endif
    endif

else
    LOCAL_PROPRIETARY_MODULE := true
    LOCAL_MODULE_OWNER := lenovo
    LOCAL_SRC_FILES := extend_reclaim.c

    ifeq ($(TARGET_OUT_VENDOR),)
        LOCAL_MODULE_PATH := $(ALPS_OUT)/vendor/lib/modules
    else
        LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/lib/modules
    endif

    include $(MTK_KERNEL_MODULE)
endif
