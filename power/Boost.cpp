/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "power-sunfish"

#include "Boost.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace aidl::android::hardware::power::impl::sunfish {

using ::android::base::ReadFileToString;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {

constexpr const char* kLittleMin = "/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq";
constexpr const char* kBigMin = "/sys/devices/system/cpu/cpufreq/policy6/scaling_min_freq";
constexpr const char* kLittleHwMin = "/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_min_freq";
constexpr const char* kBigHwMin = "/sys/devices/system/cpu/cpufreq/policy6/cpuinfo_min_freq";
constexpr const char* kLittleHwMax = "/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq";
constexpr const char* kBigHwMax = "/sys/devices/system/cpu/cpufreq/policy6/cpuinfo_max_freq";
constexpr const char* kTopAppUclampMin = "/dev/cpuctl/top-app/cpu.uclamp.min";

// Values from the stock powerhint.json (schedtune.boost -> uclamp.min %)
constexpr const char* kInteractionLittleMin = "1248000";
constexpr const char* kUclampIdle = "10";
constexpr const char* kUclampInteraction = "40";
constexpr const char* kUclampLaunch = "60";

constexpr int kMinInteractionMs = 250;
constexpr auto kLaunchTimeout = std::chrono::seconds(3);

std::string readValue(const char* path, const char* fallback) {
    std::string s;
    if (!ReadFileToString(path, &s)) return fallback;
    return Trim(s);
}

void writeValue(const char* path, const std::string& value, std::string* last) {
    if (*last == value) return;
    if (!WriteStringToFile(value, path))
        PLOG(ERROR) << "write " << value << " to " << path;
    *last = value;
}

}  // namespace

BoostManager::BoostManager() {
    mLittleMinDefault = readValue(kLittleHwMin, "300000");
    mBigMinDefault = readValue(kBigHwMin, "300000");
    mLittleMax = readValue(kLittleHwMax, "9999999");
    mBigMax = readValue(kBigHwMax, "9999999");
    mThread = std::thread(&BoostManager::loop, this);
}

BoostManager::~BoostManager() {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mStop = true;
    }
    mCv.notify_all();
    mThread.join();
}

void BoostManager::interaction(int durationMs) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        auto until = Clock::now() + std::chrono::milliseconds(
                                            std::max(durationMs, kMinInteractionMs));
        mInteractionUntil = std::max(mInteractionUntil, until);
    }
    mCv.notify_all();
}

void BoostManager::launch(bool enable) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mLaunchUntil = enable ? Clock::now() + kLaunchTimeout : Clock::time_point();
    }
    mCv.notify_all();
}

// Called with mLock held
void BoostManager::apply() {
    auto now = Clock::now();
    bool launch = now < mLaunchUntil;
    bool interaction = now < mInteractionUntil;

    // Raise maximums before minimums never matters here: max is untouched.
    writeValue(kLittleMin,
               launch ? mLittleMax : interaction ? kInteractionLittleMin : mLittleMinDefault,
               &mLittleMin);
    writeValue(kBigMin, launch ? mBigMax : mBigMinDefault, &mBigMin);
    writeValue(kTopAppUclampMin,
               launch ? kUclampLaunch : interaction ? kUclampInteraction : kUclampIdle,
               &mUclamp);
}

void BoostManager::loop() {
    std::unique_lock<std::mutex> lock(mLock);
    while (!mStop) {
        apply();
        auto next = Clock::time_point::max();
        auto now = Clock::now();
        if (mInteractionUntil > now) next = std::min(next, mInteractionUntil);
        if (mLaunchUntil > now) next = std::min(next, mLaunchUntil);
        if (next == Clock::time_point::max())
            mCv.wait(lock);
        else
            mCv.wait_until(lock, next);
    }
}

}  // namespace aidl::android::hardware::power::impl::sunfish
