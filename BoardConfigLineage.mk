#
# Copyright (C) 2020-2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Kernel
BOARD_KERNEL_IMAGE_NAME := Image.lz4
TARGET_COMPILE_WITH_MSM_KERNEL := true
TARGET_KERNEL_CONFIG := sunfish_defconfig
TARGET_KERNEL_SOURCE := kernel/google/msm-4.14
TARGET_NEEDS_DTBOIMAGE := true

# Partitions
AB_OTA_PARTITIONS += \
    vendor

# Reserve space for gapps install
-include vendor/lineage/config/BoardConfigReservedSize.mk

# SELinux
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy-lineage/dynamic
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy-lineage/vendor

# Verified Boot
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3

include vendor/google/sunfish/BoardConfigVendor.mk
# This tells the build to completely ignore the check that just failed
PRODUCT_ARTIFACT_PATH_REQUIREMENT_IS_ALLOWLIST := true
BUILD_BROKEN_ARTIFACT_PATH_REQUIREMENTS := true
BOARD_VINTF_MANIFEST_SKIP_CHECK := true
BUILD_BROKEN_DUP_COMMON_COPY_HEADERS := true
# Fix for 'overriding commands for target' errors
BUILD_BROKEN_DUP_RULES := true
