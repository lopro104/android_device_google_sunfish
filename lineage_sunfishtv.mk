#
# Copyright (C) 2020-2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_tv.mk)

# Inherit device configuration
$(call inherit-product, device/google/sunfishtv/aosp_sunfishtv.mk)

include device/google/sunfishtv/device-lineage.mk

# Device identifier. This must come after all inclusions
PRODUCT_BRAND := google
PRODUCT_MODEL := Pixel 4a TV
PRODUCT_NAME := lineage_sunfishtv

# Boot animation
TARGET_SCREEN_HEIGHT := 2340
TARGET_SCREEN_WIDTH := 1080

#Flags:
EVO_BUILD_TYPE := Unofficial
WITH_GMS := true
TARGET_USES_MINI_GAPPS := false
TARGET_USES_PICO_GAPPS := false
BUILD_BCR := true
TARGET_HAS_UDFPS := false
TARGET_INCLUDE_ACCORD := false
TARGET_DISABLE_EPPE := false

PRODUCT_DEFAULT_DEV_CERTIFICATE := vendor/evolution-priv/keys/releasekey
PRODUCT_OTACERT := vendor/evolution-priv/keys/releasekey

PRODUCT_COMPRESSED_APEX := true

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="sunfish-user 13 TQ3A.230805.001.S1 10786265 release-keys" \
    BuildFingerprint=google/sunfish/sunfish:13/TQ3A.230805.001.S1/10786265:user/release-keys \
    DeviceProduct=sunfishtv

$(call inherit-product, vendor/google/sunfish/sunfish-vendor.mk)

PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := false
