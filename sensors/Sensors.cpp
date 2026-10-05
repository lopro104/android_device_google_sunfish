/*
 * Copyright (C) 2021 The Android Open Source Project
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "sensors-sunfish"

#include "Sensors.h"

#include <hardware_legacy/power.h>
#include <log/log.h>
#include <unistd.h>
#include <utils/SystemClock.h>
#include <utils/Timers.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace aidl::android::hardware::sensors {

using ::ndk::ScopedAStatus;
using EventPayload = Event::EventPayload;

namespace {

// SEE message ids of the events we turn into Android events.
constexpr uint32_t kMsgStdSensorEvent = 1025;
constexpr uint32_t kMsgProximityEvent = 769;

// sensorspd only starts once /data is up, and SensorService (system_server)
// starts after that, so keep waiting rather than registering an empty list.
constexpr int kServiceTimeoutMs = 10000;
constexpr int kLookupTimeoutMs = 3000;

constexpr uint32_t kOnChange = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE);
constexpr uint32_t kWakeUp = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);

struct SensorDef {
    const char* dataType;
    SensorType type;
    const char* name;
    const char* vendor;
    float maxRange, resolution, power;
    int32_t minDelayUs, maxDelayUs;
    uint32_t flags;
    int32_t fifoReserved;
};

// Matches what the stock (downstream) HAL reports for sunfish.
const SensorDef kSensorDefs[] = {
        {"accel", SensorType::ACCELEROMETER, "LSM6DSR Accelerometer", "STMicro", 156.9064f,
         0.0047884f, 0.15f, 2404, 1000000, 0, 3000},
        {"gyro", SensorType::GYROSCOPE, "LSM6DSR Gyroscope", "STMicro", 34.906586f, 0.0012217f,
         0.55f, 2404, 1000000, 0, 0},
        {"mag", SensorType::MAGNETIC_FIELD, "LIS2MDL Magnetometer", "STMicro", 4912.0f, 0.15f,
         0.6f, 10000, 1000000, 0, 600},
        {"pressure", SensorType::PRESSURE, "BMP380 Pressure Sensor", "Bosch", 1100.0f, 0.0017f,
         0.004f, 40000, 1000000, 0, 300},
        {"ambient_light", SensorType::LIGHT, "TCS3701 Ambient Light Sensor", "AMS", 60000.0f,
         0.01f, 0.1f, 0, 0, kOnChange, 0},
        {"proximity", SensorType::PROXIMITY, "TCS3701 Proximity Sensor (wake-up)", "AMS", 5.0f,
         5.0f, 0.1f, 0, 0, kOnChange | kWakeUp, 300},
};

// sns_std_sensor_event: 1 repeated float data (packed or not), 2 status
int readStdEvent(const uint8_t* d, size_t len, float* out, int max, int8_t* status) {
    int n = 0;
    *status = 0;
    for (const uint8_t* p = d; p < d + len;) {
        uint8_t key = *p++;
        int field = key >> 3, wt = key & 7;
        if (field == 1 && wt == 5 && p + 4 <= d + len) {
            if (n < max) memcpy(&out[n++], p, 4);
            p += 4;
        } else if (field == 1 && wt == 2 && p < d + len) {
            size_t l = *p++;  // short packed arrays only
            for (size_t i = 0; i + 4 <= l && p + i + 4 <= d + len; i += 4)
                if (n < max) memcpy(&out[n++], p + i, 4);
            p += l;
        } else if (field == 2 && wt == 0 && p < d + len) {
            *status = *p++ & 0x7f;
        } else {
            break;
        }
    }
    return n;
}

}  // namespace

Sensors::Sensors() {
    while (!mSsc.start(
            [this](const ssc::Suid& suid, uint32_t msgId, int64_t ts, const uint8_t* d,
                   size_t len) { onSscEvent(suid, msgId, ts, d, len); },
            kServiceTimeoutMs))
        ALOGW("waiting for the SEE service");

    auto lookupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    int32_t handle = 1;
    for (const auto& def : kSensorDefs) {
        // Early in boot the SEE answers lookups with no sensors until
        // sensorspd has loaded the registry, so keep asking for a while.
        ssc::Suid suid;
        while (!(suid = mSsc.lookup(def.dataType, kLookupTimeoutMs)).valid() &&
               std::chrono::steady_clock::now() < lookupDeadline)
            usleep(500000);
        if (!suid.valid()) {
            ALOGW("no SEE sensor for %s", def.dataType);
            continue;
        }

        SscSensor s;
        s.dataType = def.dataType;
        s.suid = suid;
        s.info.sensorHandle = handle;
        s.info.name = def.name;
        s.info.vendor = def.vendor;
        s.info.version = 1;
        s.info.type = def.type;
        s.info.typeAsString = "";
        s.info.maxRange = def.maxRange;
        s.info.resolution = def.resolution;
        s.info.power = def.power;
        s.info.minDelayUs = def.minDelayUs;
        s.info.maxDelayUs = def.maxDelayUs;
        s.info.fifoReservedEventCount = def.fifoReserved;
        s.info.fifoMaxEventCount = 10000;
        s.info.requiredPermission = "";
        s.info.flags = def.flags;
        if (!(def.flags & kOnChange)) s.rateHz = 1e6f / def.maxDelayUs;

        ALOGI("%s: handle %d suid %016llx%016llx", def.dataType, handle,
              (unsigned long long)suid.hi, (unsigned long long)suid.lo);
        mHandleBySuid[suid] = handle;
        mSensors[handle++] = s;
    }
}

Sensors::~Sensors() {
    {
        std::lock_guard<std::mutex> lock(mWriteLock);
        deleteEventFlagLocked();
    }
    mReadWakeLockQueueRun = false;
    if (mWakeLockThread.joinable()) mWakeLockThread.join();
}

void Sensors::onSscEvent(const ssc::Suid& suid, uint32_t msgId, int64_t ts, const uint8_t* d,
                         size_t len) {
    Event ev;
    bool wakeup;
    {
        std::lock_guard<std::mutex> lock(mSensorsLock);
        auto h = mHandleBySuid.find(suid);
        if (h == mHandleBySuid.end()) return;
        const SscSensor& s = mSensors[h->second];
        if (!s.enabled) return;

        ev.sensorHandle = s.info.sensorHandle;
        ev.sensorType = s.info.type;
        ev.timestamp = ts;
        wakeup = s.info.flags & kWakeUp;

        if (s.info.type == SensorType::PROXIMITY) {
            // sns_proximity_event: 1 event type (0 far, 1 near), 2 raw adc, 3 status
            if (msgId != kMsgProximityEvent || len < 2 || d[0] != 0x08) return;
            bool near = d[1] == 1;
            ev.payload.set<EventPayload::Tag::scalar>(near ? 0.0f : s.info.maxRange);
        } else {
            float v[8];
            int8_t status;
            if (msgId != kMsgStdSensorEvent) return;
            int n = readStdEvent(d, len, v, 8, &status);
            switch (s.info.type) {
                case SensorType::ACCELEROMETER:
                case SensorType::GYROSCOPE:
                case SensorType::MAGNETIC_FIELD: {
                    if (n < 3) return;
                    EventPayload::Vec3 vec = {v[0], v[1], v[2],
                                              static_cast<SensorStatus>(status)};
                    ev.payload.set<EventPayload::Tag::vec3>(vec);
                    break;
                }
                default:
                    if (n < 1) return;
                    ev.payload.set<EventPayload::Tag::scalar>(v[0]);
                    break;
            }
        }
    }
    postEvents({ev}, wakeup);
}

ScopedAStatus Sensors::activate(int32_t handle, bool enabled) {
    std::lock_guard<std::mutex> lock(mSensorsLock);
    auto it = mSensors.find(handle);
    if (it == mSensors.end()) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);

    SscSensor& s = it->second;
    if (s.enabled == enabled) return ScopedAStatus::ok();
    s.enabled = enabled;
    if (enabled)
        mSsc.enable(s.suid, s.rateHz, s.info.flags & kWakeUp);
    else
        mSsc.disable(s.suid);
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::batch(int32_t handle, int64_t samplingPeriodNs,
                             int64_t /* maxReportLatencyNs */) {
    std::lock_guard<std::mutex> lock(mSensorsLock);
    auto it = mSensors.find(handle);
    if (it == mSensors.end()) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);

    SscSensor& s = it->second;
    if (s.info.flags & kOnChange) return ScopedAStatus::ok();

    int64_t minNs = s.info.minDelayUs * 1000LL, maxNs = s.info.maxDelayUs * 1000LL;
    samplingPeriodNs = std::clamp(samplingPeriodNs, minNs, maxNs);
    float rate = 1e9f / samplingPeriodNs;
    if (rate != s.rateHz) {
        s.rateHz = rate;
        if (s.enabled) mSsc.enable(s.suid, s.rateHz, s.info.flags & kWakeUp);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::flush(int32_t handle) {
    Event ev;
    bool wakeup;
    {
        std::lock_guard<std::mutex> lock(mSensorsLock);
        auto it = mSensors.find(handle);
        if (it == mSensors.end()) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        if (!it->second.enabled)
            return ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(BnSensors::ERROR_BAD_VALUE));
        wakeup = it->second.info.flags & kWakeUp;
    }

    // Events are not batched, so there is nothing to drain first.
    ev.sensorHandle = handle;
    ev.sensorType = SensorType::META_DATA;
    ev.timestamp = 0;
    EventPayload::MetaData meta = {
            .what = EventPayload::MetaData::MetaDataEventType::META_DATA_FLUSH_COMPLETE,
    };
    ev.payload.set<EventPayload::Tag::meta>(meta);
    postEvents({ev}, wakeup);
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::getSensorsList(std::vector<SensorInfo>* out) {
    std::lock_guard<std::mutex> lock(mSensorsLock);
    for (const auto& [handle, s] : mSensors) out->push_back(s.info);
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::initialize(
        const MQDescriptor<Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
        const MQDescriptor<int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
        const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) {
    ScopedAStatus result = ScopedAStatus::ok();

    ALOGI("Sensors initializing");
    for (const auto& [handle, s] : mSensors) activate(handle, false);

    if (mReadWakeLockQueueRun.load()) {
        mReadWakeLockQueueRun = false;
        mWakeLockThread.join();
    }

    mCallback = in_sensorsCallback;
    {
        std::lock_guard<std::mutex> lock(mWriteLock);

        mEventQueue = std::make_unique<AidlMessageQueue<Event, SynchronizedReadWrite>>(
                in_eventQueueDescriptor, true /* resetPointers */);
        deleteEventFlagLocked();
        if (EventFlag::createEventFlag(mEventQueue->getEventFlagWord(), &mEventQueueFlag) !=
            ::android::OK)
            result = ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);

        mWakeLockQueue = std::make_unique<AidlMessageQueue<int32_t, SynchronizedReadWrite>>(
                in_wakeLockDescriptor, true /* resetPointers */);
        if (!mCallback || !mEventQueue || !mWakeLockQueue || mEventQueueFlag == nullptr)
            result = ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    mReadWakeLockQueueRun = true;
    mWakeLockThread = std::thread(&Sensors::readWakeLockFMQ, this);
    return result;
}

ScopedAStatus Sensors::injectSensorData(const Event& event) {
    if (event.sensorType == SensorType::ADDITIONAL_INFO) return ScopedAStatus::ok();
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::setOperationMode(ISensors::OperationMode mode) {
    if (mode == ISensors::OperationMode::NORMAL) return ScopedAStatus::ok();
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::configDirectReport(int32_t, int32_t, ISensors::RateLevel,
                                          int32_t* _aidl_return) {
    *_aidl_return = EX_UNSUPPORTED_OPERATION;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::registerDirectChannel(const ISensors::SharedMemInfo&,
                                             int32_t* _aidl_return) {
    *_aidl_return = EX_UNSUPPORTED_OPERATION;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::unregisterDirectChannel(int32_t) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

void Sensors::postEvents(const std::vector<Event>& events, bool wakeup) {
    std::lock_guard<std::mutex> lock(mWriteLock);
    if (mEventQueue == nullptr) return;
    if (mEventQueue->write(&events.front(), events.size())) {
        if (mEventQueueFlag == nullptr) return;
        mEventQueueFlag->wake(
                static_cast<uint32_t>(BnSensors::EVENT_QUEUE_FLAG_BITS_READ_AND_PROCESS));
        if (wakeup) updateWakeLock(events.size(), 0);
    }
}

void Sensors::deleteEventFlagLocked() {
    if (mEventQueueFlag != nullptr) {
        if (EventFlag::deleteEventFlag(&mEventQueueFlag) != ::android::OK)
            ALOGI("Failed to delete event flag");
    }
}

void Sensors::readWakeLockFMQ() {
    while (mReadWakeLockQueueRun.load()) {
        constexpr int64_t kReadTimeoutNs = 500 * 1000 * 1000;
        int32_t eventsHandled = 0;

        mWakeLockQueue->readBlocking(&eventsHandled, 1, 0,
                                     static_cast<uint32_t>(WAKE_LOCK_QUEUE_FLAG_BITS_DATA_WRITTEN),
                                     kReadTimeoutNs);
        updateWakeLock(0, eventsHandled);
    }
}

void Sensors::updateWakeLock(int32_t eventsWritten, int32_t eventsHandled) {
    std::lock_guard<std::mutex> lock(mWakeLockLock);
    int32_t newVal = mOutstandingWakeUpEvents + eventsWritten - eventsHandled;
    mOutstandingWakeUpEvents = newVal < 0 ? 0 : newVal;

    if (eventsWritten > 0)
        mAutoReleaseWakeLockTime =
                ::android::uptimeMillis() + static_cast<uint32_t>(WAKE_LOCK_TIMEOUT_SECONDS) * 1000;

    if (!mHasWakeLock && mOutstandingWakeUpEvents > 0 &&
        acquire_wake_lock(PARTIAL_WAKE_LOCK, kWakeLockName) == 0) {
        mHasWakeLock = true;
    } else if (mHasWakeLock) {
        if (::android::uptimeMillis() > mAutoReleaseWakeLockTime) mOutstandingWakeUpEvents = 0;
        if (mOutstandingWakeUpEvents == 0 && release_wake_lock(kWakeLockName) == 0)
            mHasWakeLock = false;
    }
}

}  // namespace aidl::android::hardware::sensors
