#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/google/sunfish_mainline

# Inherit options from mainline/qcom-common
## SoC
TARGET_QCOM_SOC := sm7150-ab
## TODO: Bringup the corresponding hardware and remove the following definitions
TARGET_SUPPORTS_SUSPEND := false
include device/mainline/qcom-common/optional/options.mk

# Inherit from mainline/qcom-common
$(call inherit-product, device/mainline/qcom-common/mainline_qcom-common.mk)

# AAPT
PRODUCT_AAPT_PREF_CONFIG := xxhdpi

# Boot animation
TARGET_BOOTANIMATION_HALF_RES := true

TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_HEIGHT := 2340

# Dalvik heap
$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)

# DSP
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(DEVICE_PATH)/socinfo/,$(TARGET_COPY_OUT_VENDOR)/etc/hexagonrpcd-root/socinfo/) \
    $(call find-copy-subdir-files,*,vendor/google/sunfish/proprietary/vendor/etc/acdbdata/,$(TARGET_COPY_OUT_VENDOR)/etc/hexagonrpcd-root/acdb/) \
    $(call find-copy-subdir-files,*,vendor/google/sunfish/proprietary/vendor/etc/sensors/config/,$(TARGET_COPY_OUT_VENDOR)/etc/hexagonrpcd-root/sensors/config/) \
    vendor/google/sunfish/proprietary/vendor/etc/sensors/sns_reg_config:$(TARGET_COPY_OUT_VENDOR)/etc/hexagonrpcd-root/sensors/sns_reg.conf

# Dynamic partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Firmware
PRODUCT_COPY_FILES += \
    vendor/google/sunfish/proprietary/vendor/firmware/a615_zap.elf:$(TARGET_COPY_OUT_ODM)/firmware/qcom/sm7150/google/sunfish/a615_zap.mbn

# adsp/cdsp firmware lives in the vendor image on Pixels (only the modem
# firmware comes from the modem partition mounted at /vendor/firmware_mnt)
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,adsp.*,vendor/google/sunfish/proprietary/vendor/firmware/,$(TARGET_COPY_OUT_VENDOR)/firmware/) \
    $(call find-copy-subdir-files,adsp*.jsn,vendor/google/sunfish/proprietary/vendor/firmware/,$(TARGET_COPY_OUT_VENDOR)/firmware/) \
    $(call find-copy-subdir-files,cdsp.*,vendor/google/sunfish/proprietary/vendor/firmware/,$(TARGET_COPY_OUT_VENDOR)/firmware/) \
    $(call find-copy-subdir-files,cdsp*.jsn,vendor/google/sunfish/proprietary/vendor/firmware/,$(TARGET_COPY_OUT_VENDOR)/firmware/) \
    vendor/google/sunfish/proprietary/vendor/firmware/modemuw.jsn:$(TARGET_COPY_OUT_VENDOR)/firmware/modemuw.jsn \
    vendor/google/sunfish/proprietary/vendor/firmware/wlanmdsp.mbn:$(TARGET_COPY_OUT_VENDOR)/firmware/wlanmdsp.mbn

PRODUCT_PACKAGES += \
    all_symlink_firmware_sunfish \
    firmware_sunfish_ipa_fws.mbn

# HIDL
PRODUCT_PACKAGES += \
    vndservicemanager

# Init
PRODUCT_PACKAGES += \
    fstab.sunfish \
    fstab.sunfish.vendor_ramdisk \
    init.sunfish.rc \
    init.recovery.sunfish.rc \
    ueventd.sunfish.rc

# Also place the fstab in the generic (boot.img) ramdisk so the direct-ABL
# boot path works without GRUB concatenating the vendor_boot fragments.
#
# CRITICAL: this is BOARD_USES_RECOVERY_AS_BOOT, so first-stage init does
# force_normal_boot -> mkdir /first_stage_ramdisk -> SwitchRoot("/first_stage_ramdisk")
# BEFORE reading the fstab. After the pivot it looks for the fstab at
# (new root)/system/etc/fstab.<hw> == ramdisk/first_stage_ramdisk/system/etc/.
# Installing only to the ramdisk root leaves it behind the switch_root, so init
# panics with "ReadDefaultFstab(): failed to find device default fstab". Install
# into first_stage_ramdisk/ (post-pivot search path); keep the root copy too.
# SHOTGUN: place the fstab at every path + suffix first-stage init might search,
# so whichever convention this A17 build/init actually uses, it resolves.
# Suffixes tried by GetFstabPath: ro.boot.fstab_suffix, ro.boot.hardware.platform
# (=sm7150), ro.boot.hardware (=sunfish). Locations: ramdisk root and (post
# switch_root) first_stage_ramdisk, both /system/etc/ (new convention) and / (old).
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/system/etc/fstab.sunfish \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/system/etc/fstab.sm7150 \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/first_stage_ramdisk/system/etc/fstab.sunfish \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/first_stage_ramdisk/system/etc/fstab.sm7150 \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/first_stage_ramdisk/fstab.sunfish \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RAMDISK)/first_stage_ramdisk/fstab.sm7150

# Boot-crash log capture -> /metadata (bringup only; see init/init.bootlog.rc).
# Installed into the vendor image so a targeted `m vendorimage` + reflash of
# vendor_a is enough to iterate — no full super rebuild.
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/init/init.bootlog.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.bootlog.rc

$(call soong_config_set,libinit,vendor_init_lib,//$(DEVICE_PATH):init_sunfish_mainline)

PRODUCT_PACKAGES += \
    use_memfd.rc

# Images
PRODUCT_BUILD_BOOT_IMAGE := true
PRODUCT_BUILD_RAMDISK_IMAGE := true
PRODUCT_BUILD_RECOVERY_IMAGE := true

# Kernel
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/modprobe/modules.blocklist:$(TARGET_COPY_OUT_VENDOR_DLKM)/lib/modules/modules.blocklist

PRODUCT_PACKAGES += \
    modules.load.normal

PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

# Overlays
DEVICE_PACKAGE_OVERLAYS += \
    $(DEVICE_PATH)/overlays/overlay

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml

# Scoped Storage
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Shipping API level
PRODUCT_SHIPPING_API_LEVEL := 33

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH) \
    kernel/mainline/configs \
    vendor/qcom/opensource/commonsys-intf/display
# ^ makes the namespaced vendor.qti.hardware.display.config-V18 visible so the
#   tree-wide (global-namespace) qacs/ambientdatacapture interface resolves its
#   import during soong analysis (downstream sunfish does the same). Not installed
#   into the mainline image; only needed to satisfy the dangling import.
