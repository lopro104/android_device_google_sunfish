/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace aidl::android::hardware::power::impl::sunfish {

/*
 * Timed CPU boosts, after the INTERACTION and LAUNCH actions of the stock
 * sunfish powerhint.json, applied through the knobs the mainline kernel
 * has: cpufreq policy minimum frequencies and the top-app uclamp.min.
 */
class BoostManager {
  public:
    using Clock = std::chrono::steady_clock;

    BoostManager();
    ~BoostManager();

    void interaction(int durationMs);
    void launch(bool enable);

  private:
    void loop();
    void apply();

    std::mutex mLock;
    std::condition_variable mCv;
    std::thread mThread;
    bool mStop = false;

    Clock::time_point mInteractionUntil;
    Clock::time_point mLaunchUntil;

    std::string mLittleMinDefault, mBigMinDefault, mLittleMax, mBigMax;
    std::string mLittleMin, mBigMin, mUclamp;  // last written values
};

}  // namespace aidl::android::hardware::power::impl::sunfish
