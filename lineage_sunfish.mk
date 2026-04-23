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

#Some apps:
TARGET_INCLUDE_LIVE_WALLPAPERS := true
PRODUCT_PACKAGES += \
    WallpaperEffect \
    PixelLiveWallpaperPrebuilt-26000013 \
    DevicePersonalizationAiAiPrebuiltPixel2025 \
    MagicPortraitWallpapers \
    MagicPortraitSymLink

# Remove specifically Tycho and Google Photos from the list
PRODUCT_PACKAGES := $(filter-out DevicePersonalizationPrebuiltPixel2020 , $(PRODUCT_PACKAGES))

#Flags:
EVO_BUILD_TYPE := Unofficial
#Gapps
WITH_GMS := true
TARGET_USES_MINI_GAPPS := true
TARGET_USES_PICO_GAPPS := false
#Other
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


