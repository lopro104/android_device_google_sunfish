PRODUCT_PUBLIC_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/public
PRODUCT_PRIVATE_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/private

# vendors
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/google
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/qcom/common
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/qcom/sm7150
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/tracking_denials
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/st
BOARD_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/verizon

# system_ext
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/system_ext/private

# Pixel-wide sepolicy
BOARD_VENDOR_SEPOLICY_DIRS += hardware/google/pixel-sepolicy/confirmationui_hal
BOARD_VENDOR_SEPOLICY_DIRS += hardware/google/pixel-sepolicy/powerstats
BOARD_VENDOR_SEPOLICY_DIRS += hardware/google/pixel-sepolicy/ramdump/common

# misc_writer
BOARD_VENDOR_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/google/misc_writer

# thermal
BOARD_VENDOR_SEPOLICY_DIRS += device/google/sunfishtv/sepolicy/vendor/google/thermal
