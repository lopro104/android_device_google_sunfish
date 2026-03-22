#
# Copyright (C) 2020-2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit device configuration
$(call inherit-product, device/google/sunfish/aosp_sunfish.mk)

include device/google/sunfish/device-lineage.mk

# Device identifier. This must come after all inclusions
PRODUCT_BRAND := google
PRODUCT_MODEL := Pixel 4a
PRODUCT_NAME := lineage_sunfish

# Boot animation
TARGET_SCREEN_HEIGHT := 2340
TARGET_SCREEN_WIDTH := 1080

#Flags:
#i dont want call recording so bye ig? also other small thigs
#EVO_BUILD_TYPE := Official
#Private build so idgaf
EVO_BUILD_TYPE := Unofficial
#Ok so i gaf now..
WITH_GMS := true
TARGET_USES_MINI_GAPPS := false
TARGET_USES_PICO_GAPPS := false
#Bye full gapps :(
BUILD_BCR := true
TARGET_HAS_UDFPS := false
TARGET_INCLUDE_ACCORD := false
TARGET_DISABLE_EPPE := false

PRODUCT_COMPRESSED_APEX := true

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="sunfish-user 13 TQ3A.230805.001.S1 10786265 release-keys" \
    BuildFingerprint=google/sunfish/sunfish:13/TQ3A.230805.001.S1/10786265:user/release-keys \
    DeviceProduct=sunfish

$(call inherit-product, vendor/google/sunfish/sunfish-vendor.mk)
# Completely disable the artifact path check that failed
PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := false

PRODUCT_DEFAULT_DEV_CERTIFICATE := android-certs/releasekey
PRODUCT_NOT_DEBUGGABLE_IN_USERDEBUG :=
