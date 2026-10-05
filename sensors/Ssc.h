/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace ssc {

struct Suid {
    uint64_t lo = 0, hi = 0;
    bool operator<(const Suid& o) const { return hi != o.hi ? hi < o.hi : lo < o.lo; }
    bool valid() const { return lo || hi; }
};

/*
 * Minimal client for the Qualcomm Snapdragon Sensor Core (SEE) reached over
 * QRTR as QMI service 400. Messages are hand-encoded protobuf, as in
 * tools/ssclist.
 */
class Client {
  public:
    // Called from the receive thread for every event of an enabled sensor.
    using EventFn = std::function<void(const Suid& suid, uint32_t msgId, int64_t timestampNs,
                                       const uint8_t* payload, size_t len)>;

    ~Client();

    // Wait up to timeoutMs for the SEE service, then start receiving.
    bool start(EventFn fn, int timeoutMs);
    // Resolve the default sensor of a data type ("accel", "ambient_light"...).
    Suid lookup(const std::string& type, int timeoutMs);
    // rateHz > 0: streaming config; rateHz == 0: on-change config.
    bool enable(const Suid& suid, float rateHz, bool wakeup);
    bool disable(const Suid& suid);

  private:
    bool findService(int timeoutMs);
    bool sendRequest(const Suid& suid, uint32_t msgId, const uint8_t* payload, size_t len,
                     bool wakeup);
    void rxLoop();
    void handleEventMsg(const uint8_t* d, size_t len);

    int mSock = -1;
    uint32_t mNode = 0, mPort = 0;
    uint16_t mTxn = 1;
    std::mutex mTxLock;

    EventFn mEventFn;
    std::thread mRxThread;
    bool mStop = false;

    std::mutex mLookupLock;
    std::condition_variable mLookupCv;
    std::map<std::string, Suid> mLookups;
};

}  // namespace ssc
