/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/GnssLocation.h>
#include <aidl/android/hardware/gnss/IGnssCallback.h>

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aidl::android::hardware::gnss {

/*
 * Client for the modem's QMI LOC service (16) over QRTR: runs a periodic
 * standalone fix session and decodes position, satellite and NMEA
 * indications. Message layouts follow location_service_v02.h.
 */
class LocClient {
  public:
    using LocationFn = std::function<void(const GnssLocation&)>;
    using SvFn = std::function<void(const std::vector<IGnssCallback::GnssSvInfo>&)>;
    using NmeaFn = std::function<void(int64_t, const std::string&)>;

    LocClient(LocationFn location, SvFn sv, NmeaFn nmea);
    ~LocClient();

    bool start(int intervalMs);
    void stop();
    void injectTime(int64_t utcMs, uint32_t uncertaintyMs);
    void injectPosition(double lat, double lon, float accuracyM);

  private:
    bool connect();
    bool send(uint16_t msgId, const std::vector<uint8_t>& tlvs);
    void rxLoop();
    void handleIndication(uint16_t msgId, const uint8_t* tlvs, size_t len);
    void handlePosition(const uint8_t* tlvs, size_t len);
    void handleSvInfo(const uint8_t* tlvs, size_t len);

    LocationFn mLocationFn;
    SvFn mSvFn;
    NmeaFn mNmeaFn;

    std::mutex mTxLock;
    std::mutex mConnectLock;
    int mSock = -1;
    uint32_t mNode = 0, mPort = 0;
    uint16_t mTxn = 1;
    std::thread mRxThread;
    bool mStop = false;
    std::vector<uint16_t> mUsedSvs;
};

}  // namespace aidl::android::hardware::gnss
