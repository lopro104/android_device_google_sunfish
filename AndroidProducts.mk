#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    aosp_sunfish_mainline:$(LOCAL_DIR)/aosp_sunfish_mainline.mk \
    lineage_sunfish_mainline:$(LOCAL_DIR)/lineage_sunfish_mainline.mk

$(foreach build_type, user userdebug eng, \
    $(eval COMMON_LUNCH_CHOICES += aosp_sunfish_mainline-$(build_type)) \
    $(eval COMMON_LUNCH_CHOICES += lineage_sunfish_mainline-$(build_type)))

