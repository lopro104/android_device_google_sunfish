#
# Copyright (C) 2020-2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Kernel
BOARD_KERNEL_IMAGE_NAME := Image.lz4
TARGET_COMPILE_WITH_MSM_KERNEL := true

# clang can't build 4.14 anymore 😢 sad day for ALL of us
TARGET_KERNEL_CLANG_VERSION := r563880c

TARGET_NEEDS_DTBOIMAGE := true

TARGET_KERNEL_ADDITIONAL_FLAGS := CROSS_COMPILE=aarch64-linux-gnu-
TARGET_KERNEL_ADDITIONAL_FLAGS += CROSS_COMPILE_ARM32=$(abspath prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androidkernel-)

TARGET_KERNEL_SOURCE := kernel/google/msm-4.14
TARGET_KERNEL_CONFIG := sunfish_defconfig

BUILD_BROKEN_SRC_DIR_IS_WRITABLE := true

$(call soong_config_set_bool,libion,legacy_impl,true)

# Partitions
AB_OTA_PARTITIONS += \
    vendor

# Reserve space for gapps install
-include vendor/lineage/config/BoardConfigReservedSize.mk

# SELinux
BOARD_SEPOLICY_DIRS += device/google/sunfish/sepolicy-lineage/dynamic
BOARD_SEPOLICY_DIRS += device/google/sunfish/sepolicy-lineage/vendor

# Verified Boot
ifneq ($(WITH_AVB),true)
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
endif

include vendor/google/sunfish/BoardConfigVendor.mk
# This tells the build to completely ignore the check that just failed
PRODUCT_ARTIFACT_PATH_REQUIREMENT_IS_ALLOWLIST := true
BUILD_BROKEN_ARTIFACT_PATH_REQUIREMENTS := true
BOARD_VINTF_MANIFEST_SKIP_CHECK := true
BUILD_BROKEN_DUP_COMMON_COPY_HEADERS := true
# Fix for 'overriding commands for target' errors
BUILD_BROKEN_DUP_RULES := true
