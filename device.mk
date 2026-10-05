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
## A/B: qcom-common only picks the boot HAL when it sees AB_OTA_UPDATER, but
## that is set in BoardConfig.mk, which is read after this product config.
TARGET_BOOT_HAL := qcom-caf-aidl

# Camera: libcamera simple pipeline + software ISP (external/libcamera-mainline)
TARGET_CAMERA_PROVIDER_HAL := libcamera
TARGET_POWER_HAL := sunfish
$(call soong_config_set,libcamera,ipa,simple)
include device/mainline/qcom-common/optional/options.mk

# Inherit from mainline/qcom-common
$(call inherit-product, device/mainline/qcom-common/mainline_qcom-common.mk)

# A/B: update_verifier marks the slot successful so ABL stops counting
# down boot attempts; update_engine handles A/B OTAs.
PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

# NFC (ST21NFCD via the st21nfc char driver, as on stock sunfish).
# The eSE (secure element) parts are left out: no driver for it yet.
PRODUCT_PACKAGES += \
    android.hardware.nfc-service.st \
    NfcOverlaySunfish

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
    frameworks/native/data/etc/android.hardware.nfc.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.uicc.xml \
    $(DEVICE_PATH)/nfc/libnfc-hal-st.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-hal-st.conf \
    $(DEVICE_PATH)/nfc/libnfc-nci.conf:$(TARGET_COPY_OUT_PRODUCT)/etc/libnfc-nci.conf \
    vendor/google/sunfish/proprietary/vendor/firmware/st54j_fw.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/st54j_fw.bin \
    vendor/google/sunfish/proprietary/vendor/firmware/st54j_conf.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/st54j_conf.bin

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

# Audio: TinyHAL routing for the mainline sound card
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/audio/audio.sunfish_mainline.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio.sunfish_mainline.xml

# Power: boosts on touch and app launch
PRODUCT_PACKAGES += \
    android.hardware.power-service.sunfish

# Sensors: AIDL HAL talking to the ADSP sensor core over QRTR.
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.sunfish

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml

# DSP-side libraries the ADSP/CDSP load from Android over FastRPC (CHRE
# drivers, sensor algorithms, audio modules). sunfish has no dsp partition;
# stock ships these in the vendor image. hexagonrpcd-root/dsp -> /vendor/dsp.
# Without them the ADSP sensor process dies in CHRE init (sar.cc chre_utils
# fatal) and takes the whole ADSP down with it.
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,vendor/google/sunfish/proprietary/vendor/dsp/,$(TARGET_COPY_OUT_VENDOR)/dsp/) \
    $(call find-copy-subdir-files,*,vendor/google/sunfish/proprietary/vendor/etc/chre/,$(TARGET_COPY_OUT_VENDOR)/etc/chre/)

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

# With BOARD_USES_RECOVERY_AS_BOOT the boot ramdisk is the recovery ramdisk,
# so the copies above never reach boot.img; mirror them there (stock sunfish
# installs its fstab to $(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk).
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.sunfish \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.sm7150 \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/system/etc/fstab.sunfish \
    $(DEVICE_PATH)/fstab/fstab.sunfish:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/system/etc/fstab.sm7150

# Boot-crash log capture -> /metadata (bringup only; see init/init.bootlog.rc).
# Installed into the vendor image so a targeted `m vendorimage` + reflash of
# vendor_a is enough to iterate — no full super rebuild.
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/init.debuglog.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.debuglog.rc \
    $(DEVICE_PATH)/debuglog.sh:$(TARGET_COPY_OUT_VENDOR)/bin/debuglog.sh

# BRINGUP: late hand-load of the touch driver to capture its hang (see rc).
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/init/init.touchtest.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.touchtest.rc

# Point the (glodroid) FF vibrator HAL at the TI DRV2624 haptics on i2c-9.
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.vibrator.hal.input_path=/sys/devices/platform/soc@0/ac0000.geniqup/a8c000.i2c/i2c-9/9-005a/input

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
