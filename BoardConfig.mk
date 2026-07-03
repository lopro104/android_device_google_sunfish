#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

USES_DEVICE_GOOGLE_SUNFISH_MAINLINE := true

# Inherit from mainline/qcom-common
include device/mainline/qcom-common/BoardConfigMainlineQcomCommon.mk

# A/B
# Note: sunfish is an A/B device, but this port only uses slot A and
# handles boot/recovery images through the ESP, like the other
# sm7150-mainline targets.
AB_OTA_UPDATER := false

# Boot parameters
BOARD_BOOTCONFIG := \
    $(MAINLINE_COMMON_ANDROIDBOOT_PARAMS) \
    $(MAINLINE_QCOM_SOC_ANDROIDBOOT_PARAMS) \
    androidboot.verifiedbootstate=orange

BOARD_KERNEL_CMDLINE := \
    $(MAINLINE_COMMON_KERNEL_PARAMS) \
    $(MAINLINE_QCOM_KERNEL_PARAMS) \
    console=tty0

BOARD_BOOTCONFIG += androidboot.selinux=permissive
BOARD_KERNEL_CMDLINE += audit=0

# Direct-ABL boot (header v2) has no bootconfig channel — vendor_boot (and its
# bootconfig section) is only loaded on the GRUB/ESP path. Mirror the
# androidboot.* params onto the kernel cmdline so init sees them either way;
# under GRUB the duplication is harmless (identical values).
BOARD_KERNEL_CMDLINE += $(BOARD_BOOTCONFIG)

BOARD_KERNEL_CMDLINE += \
    androidboot.hardware=sunfish

# Bootloader
BOARD_BOOT_HEADER_VERSION := 2
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
# Explicit sunfish DTB — ABL needs exactly this one, not a concatenated blob
BOARD_MKBOOTIMG_ARGS += --dtb $(PRODUCT_OUT)/obj/ESP/dtb/qcom/sm7150-google-sunfish.dtb
# No init_boot partition — generic ramdisk lives in boot.img
BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := false

# Kernel load addresses
# Empirically determined 2026-07-03 (fastboot oem dmesg): sunfish ABL IGNORES
# the boot.img header addresses entirely. It gunzips the kernel and requires
# the arm64 Image header text_offset field to equal 0x80000 (Google's
# downstream TEXT_OFFSET), else it aborts with "Kernel TextOffset does not
# match". Mainline hardcodes text_offset=0, so our kernel carries a one-line
# head.S patch advertising 0x80000 (see sm7150-mainline sunfish-port branch).
# The header offsets below are therefore cosmetic; values mirror stock.
BOARD_KERNEL_BASE     := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_MKBOOTIMG_ARGS += --kernel_offset 0x00008000
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset 0x01000000
BOARD_MKBOOTIMG_ARGS += --dtb_offset 0x01f00000

# Match the physical boot_a partition size on sunfish
BOARD_BOOTIMAGE_PARTITION_SIZE := 0x04000000

# dtbo partition exists on sunfish (8 MiB) but we don't build an overlay image;
# declare the size so the build system knows about it without trying to build one
BOARD_DTBOIMG_PARTITION_SIZE := 8388608

# sunfish flash block size
BOARD_FLASH_BLOCK_SIZE := 131072

# Display
TARGET_SCREEN_DENSITY := 440

# Fastboot
TARGET_BOARD_FASTBOOT_INFO_FILE := $(DEVICE_PATH)/misc/fastboot-info.txt

# Filesystem
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USERIMAGES_USE_EXT4 := true

# Kernel
BOARD_KERNEL_IMAGE_NAME := Image.gz
TARGET_KERNEL_SOURCE := kernel/mainline/sm7150-mainline

TARGET_KERNEL_CONFIG := \
    defconfig \
    efi.config \
    sm7150.config

TARGET_KERNEL_CONFIG_EXT := \
    kernel/mainline/configs/fragments/android-base-pre/common.config \
    kernel/mainline/configs/fragments/android-base-pre/arm64.config \
    kernel/configs/b/android-6.12/android-base.config \
    kernel/mainline/configs/fragments/android-base-conditional/CONFIG_ARM64-y.config \
    kernel/mainline/configs/fragments/common.config \
    kernel/mainline/configs/fragments/y/fbcon.config \
    kernel/mainline/configs/fragments/n/disable-clang-hardening-features.config \
    kernel/mainline/configs/fragments/n/faster-build-time.config \
    $(DEVICE_PATH)/kconfigs/fixups.config

# Kernel modules
BOARD_VENDOR_KERNEL_MODULES_LOAD := \
    $(strip $(shell cat $(DEVICE_PATH)/modprobe/modules.load.basic))

BOARD_RECOVERY_RAMDISK_KERNEL_MODULES_LOAD := \
    $(strip $(shell cat $(DEVICE_PATH)/modprobe/modules.load.basic))
RECOVERY_KERNEL_MODULES := \
    $(strip $(shell cat $(DEVICE_PATH)/modprobe/modules.load.basic))

TARGET_AUTO_COLLECT_KERNEL_MODULE_DEPS := true

# OTA
TARGET_OTA_ASSERT_DEVICE := sunfish_mainline,sunfish

# Partitions
# BOARD_CACHEIMAGE_PARTITION_SIZE is not backed by a real cache partition but
# must be set so that non-A/B OTA's blockimgdiff stash-size calculation doesn't
# hit an unconditional assert cache_size is not None in FindTransfers.
BOARD_CACHEIMAGE_PARTITION_SIZE := 268435456
BOARD_USES_METADATA_PARTITION := true
TARGET_COPY_OUT_VENDOR := vendor

DLKM_PARTITIONS := system_dlkm vendor_dlkm
SSI_PARTITIONS := product system system_ext
TREBLE_PARTITIONS := odm vendor
ALL_PARTITIONS := $(DLKM_PARTITIONS) $(SSI_PARTITIONS) $(TREBLE_PARTITIONS)

BOARD_SUPER_PARTITION_GROUPS := sunfish_mainline_dynpart
BOARD_SUNFISH_MAINLINE_DYNPART_PARTITION_LIST := $(ALL_PARTITIONS)

$(foreach p, $(DLKM_PARTITIONS), \
    $(eval BOARD_USES_$(call to-upper, $(p))IMAGE := true))

$(foreach p, $(call to-upper, $(ALL_PARTITIONS)), \
    $(eval BOARD_$(p)IMAGE_EXTFS_INODE_COUNT := -1) \
    $(eval BOARD_$(p)IMAGE_FILE_SYSTEM_TYPE := ext4) \
    $(eval BOARD_$(p)IMAGE_PARTITION_RESERVED_SIZE := 67108864) \
    $(eval TARGET_COPY_OUT_$(p) := $(call to-lower, $(p))))

# sunfish has a real (non-retrofit) super partition
BOARD_SUPER_PARTITION_BLOCK_DEVICES := super
BOARD_SUPER_PARTITION_METADATA_DEVICE := super
BOARD_SUPER_PARTITION_SUPER_DEVICE_SIZE := 9755951104
BOARD_SUPER_PARTITION_SIZE := $(BOARD_SUPER_PARTITION_SUPER_DEVICE_SIZE)

BOARD_SUNFISH_MAINLINE_DYNPART_SIZE := $(shell expr $(BOARD_SUPER_PARTITION_SIZE) - 4194304 )

# Properties
TARGET_VENDOR_PROP += $(DEVICE_PATH)/properties/vendor.prop

# Ramdisk
BOARD_RAMDISK_USE_LZ4 := true

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/fstab/fstab.sunfish

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)/misc

# SEPolicy
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy

# VINTF
DEVICE_MANIFEST_FILE := \
    $(DEVICE_PATH)/vintf/manifest.xml
