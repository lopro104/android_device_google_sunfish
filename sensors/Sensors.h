/*
 * Copyright (C) 2021 The Android Open Source Project
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Event/wake-lock FMQ handling follows hardware/interfaces/sensors/aidl/default.
 */

#pragma once

#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <aidl/android/hardware/sensors/BnSensors.h>
#include <fmq/AidlMessageQueue.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "Ssc.h"

namespace aidl::android::hardware::sensors {

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::android::AidlMessageQueue;
using ::android::hardware::EventFlag;

// One physical sensor served by the SEE.
struct SscSensor {
    SensorInfo info;
    std::string dataType;
    ssc::Suid suid;
    bool enabled = false;
    float rateHz = 0;  // 0 for on-change sensors
};

class Sensors : public BnSensors {
    static constexpr const char* kWakeLockName = "SensorsHAL_WAKEUP";

  public:
    Sensors();
    ~Sensors();

    ::ndk::ScopedAStatus activate(int32_t in_sensorHandle, bool in_enabled) override;
    ::ndk::ScopedAStatus batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                               int64_t in_maxReportLatencyNs) override;
    ::ndk::ScopedAStatus configDirectReport(int32_t in_sensorHandle, int32_t in_channelHandle,
                                            ISensors::RateLevel in_rate,
                                            int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus flush(int32_t in_sensorHandle) override;
    ::ndk::ScopedAStatus getSensorsList(std::vector<SensorInfo>* _aidl_return) override;
    ::ndk::ScopedAStatus initialize(
            const MQDescriptor<Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
            const MQDescriptor<int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
            const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) override;
    ::ndk::ScopedAStatus injectSensorData(const Event& in_event) override;
    ::ndk::ScopedAStatus registerDirectChannel(const ISensors::SharedMemInfo& in_mem,
                                               int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus setOperationMode(ISensors::OperationMode in_mode) override;
    ::ndk::ScopedAStatus unregisterDirectChannel(int32_t in_channelHandle) override;

  private:
    void onSscEvent(const ssc::Suid& suid, uint32_t msgId, int64_t timestampNs,
                    const uint8_t* payload, size_t len);
    void postEvents(const std::vector<Event>& events, bool wakeup);
    void updateWakeLock(int32_t eventsWritten, int32_t eventsHandled);
    void readWakeLockFMQ();
    void deleteEventFlagLocked();

    ssc::Client mSsc;
    std::mutex mSensorsLock;
    std::map<int32_t, SscSensor> mSensors;
    std::map<ssc::Suid, int32_t> mHandleBySuid;

    std::unique_ptr<AidlMessageQueue<Event, SynchronizedReadWrite>> mEventQueue;
    std::unique_ptr<AidlMessageQueue<int32_t, SynchronizedReadWrite>> mWakeLockQueue;
    EventFlag* mEventQueueFlag = nullptr;
    std::shared_ptr<ISensorsCallback> mCallback;
    std::mutex mWriteLock;

    std::mutex mWakeLockLock;
    uint32_t mOutstandingWakeUpEvents = 0;
    std::thread mWakeLockThread;
    std::atomic_bool mReadWakeLockQueueRun = false;
    int64_t mAutoReleaseWakeLockTime = 0;
    bool mHasWakeLock = false;
};

}  // namespace aidl::android::hardware::sensors
