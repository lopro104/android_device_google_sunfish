/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "gnss-sunfish"

#include "LocClient.h"

#include <linux/qrtr.h>
#include <log/log.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <utils/SystemClock.h>

#include <algorithm>
#include <cstring>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

namespace aidl::android::hardware::gnss {

using GnssSvInfo = IGnssCallback::GnssSvInfo;
using GnssSvFlags = IGnssCallback::GnssSvFlags;

namespace {

constexpr uint32_t kLocService = 16;

constexpr uint16_t kInformClientRevision = 0x0020;
constexpr uint16_t kRegEvents = 0x0021;
constexpr uint16_t kStart = 0x0022;
constexpr uint16_t kStop = 0x0023;
constexpr uint16_t kPositionInd = 0x0024;
constexpr uint16_t kSvInfoInd = 0x0025;
constexpr uint16_t kNmeaInd = 0x0026;
constexpr uint16_t kInjectUtcTime = 0x0038;
constexpr uint16_t kInjectPosition = 0x0039;
constexpr uint16_t kSetOperationMode = 0x004a;

constexpr uint8_t kSessionId = 1;

void putTlv(std::vector<uint8_t>& v, uint8_t type, const void* data, uint16_t len) {
    v.push_back(type);
    v.push_back(len & 0xff);
    v.push_back(len >> 8);
    const uint8_t* d = static_cast<const uint8_t*>(data);
    v.insert(v.end(), d, d + len);
}

template <typename T>
void putTlv(std::vector<uint8_t>& v, uint8_t type, T value) {
    putTlv(v, type, &value, sizeof(value));
}

const uint8_t* findTlv(const uint8_t* p, size_t len, uint8_t type, uint16_t* tlvLen) {
    size_t off = 0;
    while (off + 3 <= len) {
        uint16_t l;
        memcpy(&l, p + off + 1, 2);
        if (off + 3 + l > len) break;
        if (p[off] == type) {
            *tlvLen = l;
            return p + off + 3;
        }
        off += 3 + l;
    }
    return nullptr;
}

template <typename T>
bool getTlv(const uint8_t* p, size_t len, uint8_t type, T* out) {
    uint16_t l;
    const uint8_t* v = findTlv(p, len, type, &l);
    if (!v || l < sizeof(T)) return false;
    memcpy(out, v, sizeof(T));
    return true;
}

/* QMI LOC SV system -> Android constellation and svid convention */
bool mapSv(uint32_t system, uint16_t id, GnssConstellationType* c, int* svid) {
    switch (system) {
        case 1:
            *c = GnssConstellationType::GPS;
            *svid = id;
            return true;
        case 2:
            *c = GnssConstellationType::GALILEO;
            *svid = id > 300 ? id - 300 : id;
            return true;
        case 3:
            *c = GnssConstellationType::SBAS;
            *svid = id;
            return true;
        case 5:
            *c = GnssConstellationType::GLONASS;
            *svid = id > 64 ? id - 64 : id;
            return true;
        case 4:
        case 6:
            *c = GnssConstellationType::BEIDOU;
            *svid = id > 200 ? id - 200 : id;
            return true;
        case 7:
            *c = GnssConstellationType::QZSS;
            *svid = id;
            return true;
        case 8:
            *c = GnssConstellationType::IRNSS;
            *svid = id > 400 ? id - 400 : id;
            return true;
    }
    return false;
}

}  // namespace

LocClient::LocClient(LocationFn location, SvFn sv, NmeaFn nmea)
    : mLocationFn(std::move(location)), mSvFn(std::move(sv)), mNmeaFn(std::move(nmea)) {}

LocClient::~LocClient() {
    stop();
    mStop = true;
    if (mRxThread.joinable()) mRxThread.join();
    if (mSock >= 0) close(mSock);
}

bool LocClient::connect() {
    std::lock_guard<std::mutex> lock(mConnectLock);
    if (mSock >= 0) return true;

    struct sockaddr_qrtr sq;
    socklen_t sl = sizeof(sq);
    struct qrtr_ctrl_pkt pkt;
    int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (s < 0 || getsockname(s, (struct sockaddr*)&sq, &sl)) {
        ALOGE("qrtr socket: %s", strerror(errno));
        if (s >= 0) close(s);
        return false;
    }
    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
    pkt.server.service = kLocService;
    sq.sq_port = QRTR_PORT_CTRL;
    sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr*)&sq, sizeof(sq));
    struct timeval tv = {2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    bool found = false;
    while (recv(s, &pkt, sizeof(pkt), 0) > 0) {
        if (pkt.cmd != QRTR_TYPE_NEW_SERVER || !pkt.server.service) break;
        mNode = pkt.server.node;
        mPort = pkt.server.port;
        found = true;
        break;
    }
    close(s);
    if (!found) {
        ALOGE("QMI LOC service not found");
        return false;
    }

    mSock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (mSock < 0) return false;
    ALOGI("QMI LOC on node %u port %u", mNode, mPort);
    mRxThread = std::thread(&LocClient::rxLoop, this);

    std::vector<uint8_t> t;
    putTlv(t, 0x01, uint32_t(2)); /* client revision */
    send(kInformClientRevision, t);

    /* position, satellites, NMEA; we are the Android framework (AFW) client */
    t.clear();
    putTlv(t, 0x01, uint64_t(1 << 0 | 1 << 1 | 1 << 2));
    putTlv(t, 0x11, uint32_t(1));
    send(kRegEvents, t);

    t.clear();
    putTlv(t, 0x01, uint32_t(4)); /* standalone */
    send(kSetOperationMode, t);
    return true;
}

bool LocClient::send(uint16_t msgId, const std::vector<uint8_t>& tlvs) {
    std::lock_guard<std::mutex> lock(mTxLock);
    std::vector<uint8_t> buf(7);
    uint16_t len = tlvs.size();
    buf[0] = 0; /* request */
    memcpy(&buf[1], &mTxn, 2);
    mTxn++;
    memcpy(&buf[3], &msgId, 2);
    memcpy(&buf[5], &len, 2);
    buf.insert(buf.end(), tlvs.begin(), tlvs.end());

    struct sockaddr_qrtr sq = {};
    sq.sq_family = AF_QIPCRTR;
    sq.sq_node = mNode;
    sq.sq_port = mPort;
    if (sendto(mSock, buf.data(), buf.size(), 0, (struct sockaddr*)&sq, sizeof(sq)) < 0) {
        ALOGE("send 0x%04x: %s", msgId, strerror(errno));
        return false;
    }
    return true;
}

bool LocClient::start(int intervalMs) {
    if (!connect()) return false;

    std::vector<uint8_t> t;
    putTlv(t, 0x01, kSessionId);
    putTlv(t, 0x10, uint32_t(1)); /* periodic fixes */
    putTlv(t, 0x12, uint32_t(1)); /* intermediate reports on */
    putTlv(t, 0x13, uint32_t(std::max(intervalMs, 1000)));
    return send(kStart, t);
}

void LocClient::stop() {
    if (mSock < 0) return;
    std::vector<uint8_t> t;
    putTlv(t, 0x01, kSessionId);
    send(kStop, t);
}

void LocClient::injectTime(int64_t utcMs, uint32_t uncertaintyMs) {
    if (!connect()) return;
    std::vector<uint8_t> t;
    putTlv(t, 0x01, uint64_t(utcMs));
    putTlv(t, 0x02, uncertaintyMs);
    send(kInjectUtcTime, t);
}

void LocClient::injectPosition(double lat, double lon, float accuracyM) {
    if (!connect()) return;
    std::vector<uint8_t> t;
    putTlv(t, 0x10, lat);
    putTlv(t, 0x11, lon);
    putTlv(t, 0x12, accuracyM);
    putTlv(t, 0x13, uint8_t(68)); /* horizontal confidence, % */
    send(kInjectPosition, t);
}

void LocClient::rxLoop() {
    static uint8_t buf[16384];

    while (!mStop) {
        /* qrtr_recvmsg() holds the socket lock while blocking: poll first */
        struct pollfd pfd = {mSock, POLLIN, 0};
        if (poll(&pfd, 1, 500) <= 0) continue;
        ssize_t n = recv(mSock, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 7) continue;

        uint16_t msgId, len;
        memcpy(&msgId, buf + 3, 2);
        memcpy(&len, buf + 5, 2);
        if (7 + len > n) continue;

        if (buf[0] == 2) { /* response: TLV 0x02 = result, error */
            uint16_t r[2];
            if (getTlv(buf + 7, len, 0x02, &r) && r[0])
                ALOGW("request 0x%04x failed: error %u", msgId, r[1]);
            continue;
        }
        if (buf[0] == 4) handleIndication(msgId, buf + 7, len);
    }
}

void LocClient::handleIndication(uint16_t msgId, const uint8_t* tlvs, size_t len) {
    switch (msgId) {
        case kPositionInd:
            handlePosition(tlvs, len);
            break;
        case kSvInfoInd:
            handleSvInfo(tlvs, len);
            break;
        case kNmeaInd: {
            uint16_t l;
            const uint8_t* s = findTlv(tlvs, len, 0x01, &l);
            if (s && mNmeaFn) mNmeaFn(::android::uptimeMillis(), std::string((const char*)s, l));
            break;
        }
    }
}

void LocClient::handlePosition(const uint8_t* tlvs, size_t len) {
    uint32_t status = 0xff;
    double lat, lon;
    float f;
    uint64_t utc;

    getTlv(tlvs, len, 0x01, &status);
    /* 0 = success; intermediate (1) reports carry usable fixes too */
    if (status > 1 || !getTlv(tlvs, len, 0x10, &lat) || !getTlv(tlvs, len, 0x11, &lon))
        return;

    GnssLocation loc;
    loc.gnssLocationFlags = GnssLocation::HAS_LAT_LONG;
    loc.latitudeDegrees = lat;
    loc.longitudeDegrees = lon;

    if (getTlv(tlvs, len, 0x12, &f)) {
        /* intermediate reports start out with a 9999 km uncertainty */
        if (f > 5000.0f) return;
        loc.horizontalAccuracyMeters = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_HORIZONTAL_ACCURACY;
    }
    if (getTlv(tlvs, len, 0x1A, &f)) {
        loc.altitudeMeters = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_ALTITUDE;
    }
    if (getTlv(tlvs, len, 0x1C, &f)) {
        loc.verticalAccuracyMeters = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_VERTICAL_ACCURACY;
    }
    if (getTlv(tlvs, len, 0x18, &f)) {
        loc.speedMetersPerSec = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_SPEED;
    }
    if (getTlv(tlvs, len, 0x19, &f)) {
        loc.speedAccuracyMetersPerSecond = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_SPEED_ACCURACY;
    }
    if (getTlv(tlvs, len, 0x20, &f)) {
        loc.bearingDegrees = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_BEARING;
    }
    if (getTlv(tlvs, len, 0x21, &f)) {
        loc.bearingAccuracyDegrees = f;
        loc.gnssLocationFlags |= GnssLocation::HAS_BEARING_ACCURACY;
    }
    loc.timestampMillis = getTlv(tlvs, len, 0x25, &utc) ? utc : ::android::elapsedRealtime();
    loc.elapsedRealtime.flags = ElapsedRealtime::HAS_TIMESTAMP_NS;
    loc.elapsedRealtime.timestampNs = ::android::elapsedRealtimeNano();

    /* satellites used in this fix (TLV 0x2C: u8 count + u16 ids) */
    uint16_t l;
    const uint8_t* used = findTlv(tlvs, len, 0x2C, &l);
    mUsedSvs.clear();
    if (used && l >= 1) {
        for (unsigned i = 0; i < used[0] && 1 + 2 * i + 2 <= l; i++) {
            uint16_t id;
            memcpy(&id, used + 1 + 2 * i, 2);
            mUsedSvs.push_back(id);
        }
    }

    if (mLocationFn) mLocationFn(loc);
}

void LocClient::handleSvInfo(const uint8_t* tlvs, size_t len) {
    /* TLV 0x10: u8 count + packed qmiLocSvInfoStructT_v02 (28 bytes each) */
    constexpr size_t kEntry = 28;
    uint16_t l;
    const uint8_t* list = findTlv(tlvs, len, 0x10, &l);
    std::vector<GnssSvInfo> svs;

    if (list && l >= 1) {
        for (unsigned i = 0; i < list[0] && 1 + (i + 1) * kEntry <= l; i++) {
            const uint8_t* e = list + 1 + i * kEntry;
            uint32_t valid, system, svStatus;
            uint16_t id;
            uint8_t mask;
            float elev, azim, snr;
            memcpy(&valid, e, 4);
            memcpy(&system, e + 4, 4);
            memcpy(&id, e + 8, 2);
            memcpy(&svStatus, e + 11, 4);
            mask = e[15];
            memcpy(&elev, e + 16, 4);
            memcpy(&azim, e + 20, 4);
            memcpy(&snr, e + 24, 4);

            GnssSvInfo sv;
            if (!mapSv(system, id, &sv.constellation, &sv.svid)) continue;
            sv.cN0Dbhz = (valid & 0x80) ? snr : 0;
            sv.basebandCN0DbHz = sv.cN0Dbhz > 0 ? sv.cN0Dbhz - 1 : 0;
            sv.elevationDegrees = (valid & 0x20) ? elev : 0;
            sv.azimuthDegrees = (valid & 0x40) ? azim : 0;
            sv.carrierFrequencyHz = 0;
            sv.svFlag = 0;
            if (mask & 0x1) sv.svFlag |= (int)GnssSvFlags::HAS_EPHEMERIS_DATA;
            if (mask & 0x2) sv.svFlag |= (int)GnssSvFlags::HAS_ALMANAC_DATA;
            if (std::find(mUsedSvs.begin(), mUsedSvs.end(), id) != mUsedSvs.end())
                sv.svFlag |= (int)GnssSvFlags::USED_IN_FIX;
            (void)svStatus;
            svs.push_back(sv);
        }
    }
    if (mSvFn) mSvFn(svs);
}

}  // namespace aidl::android::hardware::gnss
