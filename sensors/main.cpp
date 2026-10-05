/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Sensors.h"

using aidl::android::hardware::sensors::Sensors;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto sensors = ndk::SharedRefBase::make<Sensors>();
    const std::string name = std::string() + Sensors::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(sensors->asBinder().get(), name.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
