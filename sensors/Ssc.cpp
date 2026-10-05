/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "sensors-ssc"

#include "Ssc.h"

#include <linux/qrtr.h>
#include <log/log.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

namespace ssc {

namespace {

constexpr uint32_t kSscService = 400;
constexpr uint16_t kQmiSnsClientReq = 0x0020;
constexpr uint32_t kMsgSuidReq = 512;
constexpr uint32_t kMsgSuidEvent = 768;
constexpr uint32_t kMsgStdSensorConfig = 513;
constexpr uint32_t kMsgStdOnChangeConfig = 514;
constexpr uint32_t kMsgClientDisable = 10;
constexpr uint64_t kSuidLookupSensor = 0xABABABABABABABABULL;

/* ---- protobuf encoding ---- */
size_t putVarint(uint8_t* p, uint64_t v) {
    size_t n = 0;
    do {
        p[n] = v & 0x7f;
        v >>= 7;
        if (v) p[n] |= 0x80;
        n++;
    } while (v);
    return n;
}

size_t putTag(uint8_t* p, int field, int wt) {
    return putVarint(p, (uint64_t)field << 3 | wt);
}

size_t putBytes(uint8_t* p, int field, const void* d, size_t len) {
    size_t n = putTag(p, field, 2);
    n += putVarint(p + n, len);
    memcpy(p + n, d, len);
    return n + len;
}

size_t putFixed64(uint8_t* p, int field, uint64_t v) {
    size_t n = putTag(p, field, 1);
    memcpy(p + n, &v, 8);
    return n + 8;
}

size_t putFixed32(uint8_t* p, int field, uint32_t v) {
    size_t n = putTag(p, field, 5);
    memcpy(p + n, &v, 4);
    return n + 4;
}

size_t putUint(uint8_t* p, int field, uint64_t v) {
    size_t n = putTag(p, field, 0);
    return n + putVarint(p + n, v);
}

/* ---- protobuf decoding ---- */
struct Field {
    int field, wt;
    uint64_t v;
    const uint8_t* d;
    size_t len;
};

bool getVarint(const uint8_t** p, const uint8_t* end, uint64_t* v) {
    int shift = 0;
    *v = 0;
    while (*p < end && shift < 64) {
        uint8_t b = *(*p)++;
        *v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

bool nextField(const uint8_t** p, const uint8_t* end, Field* f) {
    uint64_t key;
    if (*p >= end || !getVarint(p, end, &key)) return false;
    f->field = key >> 3;
    f->wt = key & 7;
    switch (f->wt) {
        case 0:
            return getVarint(p, end, &f->v);
        case 1:
            if (end - *p < 8) return false;
            memcpy(&f->v, *p, 8);
            *p += 8;
            return true;
        case 5:
            if (end - *p < 4) return false;
            f->v = 0;
            memcpy(&f->v, *p, 4);
            *p += 4;
            return true;
        case 2:
            if (!getVarint(p, end, &f->v) || (uint64_t)(end - *p) < f->v) return false;
            f->d = *p;
            f->len = f->v;
            *p += f->v;
            return true;
    }
    return false;
}

Suid parseSuid(const uint8_t* d, size_t len) {
    const uint8_t *p = d, *end = d + len;
    Field f;
    Suid s;
    while (nextField(&p, end, &f)) {
        if (f.field == 1) s.lo = f.v;
        else if (f.field == 2) s.hi = f.v;
    }
    return s;
}

/*
 * SEE timestamps are QTimer ticks (19.2 MHz), the same system counter the
 * arm64 virtual counter reads, so CNTVCT gives the offset to CLOCK_BOOTTIME.
 */
int64_t ticksToBoottimeNs(uint64_t ticks) {
    uint64_t now, freq;
    struct timespec ts;

    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    clock_gettime(CLOCK_BOOTTIME, &ts);
    if (!freq) freq = 19200000;

    int64_t bootNs = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    int64_t deltaTicks = (int64_t)(now - ticks);
    return bootNs - (int64_t)((__int128)deltaTicks * 1000000000LL / (int64_t)freq);
}

}  // namespace

Client::~Client() {
    mStop = true;
    if (mSock >= 0) shutdown(mSock, SHUT_RDWR);
    if (mRxThread.joinable()) mRxThread.join();
    if (mSock >= 0) close(mSock);
}

bool Client::findService(int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    do {
        struct sockaddr_qrtr sq;
        socklen_t sl = sizeof(sq);
        struct qrtr_ctrl_pkt pkt;
        int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);

        if (s < 0) {
            ALOGE("qrtr socket: %s", strerror(errno));
            return false;
        }
        if (getsockname(s, (struct sockaddr*)&sq, &sl) == 0) {
            memset(&pkt, 0, sizeof(pkt));
            pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
            pkt.server.service = kSscService;
            sq.sq_port = QRTR_PORT_CTRL;
            if (sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr*)&sq, sizeof(sq)) >= 0) {
                struct timeval tv = {1, 0};
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                while (recv(s, &pkt, sizeof(pkt), 0) > 0) {
                    if (pkt.cmd != QRTR_TYPE_NEW_SERVER || !pkt.server.service) break;
                    if (pkt.server.service == kSscService) {
                        mNode = pkt.server.node;
                        mPort = pkt.server.port;
                        close(s);
                        return true;
                    }
                }
            }
        }
        close(s);
        usleep(250000);
    } while (std::chrono::steady_clock::now() < deadline);

    return false;
}

bool Client::start(EventFn fn, int timeoutMs) {
    if (!findService(timeoutMs)) {
        ALOGE("SEE service %u not found", kSscService);
        return false;
    }
    ALOGI("SEE service on node %u port %u", mNode, mPort);

    mSock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (mSock < 0) {
        ALOGE("qrtr socket: %s", strerror(errno));
        return false;
    }
    mEventFn = std::move(fn);
    mRxThread = std::thread(&Client::rxLoop, this);
    return true;
}

bool Client::sendRequest(const Suid& suid, uint32_t msgId, const uint8_t* payload, size_t len,
                         bool wakeup) {
    uint8_t body[256], uid[32], cfg[16], msg[512], qmi[600];
    size_t n, b, u, c, m;

    if (len > 200) return false;

    /* sns_client_request_msg: 1 suid, 2 msg_id, 3 susp_config, 4 request{2 payload} */
    b = putBytes(body, 2, payload, len);
    u = putFixed64(uid, 1, suid.lo);
    u += putFixed64(uid + u, 2, suid.hi);
    c = putUint(cfg, 1, 1);           /* client_proc_type: APSS */
    c += putUint(cfg + c, 2, !wakeup); /* delivery: 0 wakeup, 1 no-wakeup */

    m = putBytes(msg, 1, uid, u);
    m += putFixed32(msg + m, 2, msgId);
    m += putBytes(msg + m, 3, cfg, c);
    m += putBytes(msg + m, 4, body, b);

    std::lock_guard<std::mutex> lock(mTxLock);

    /* QMI header, TLV 0x01 (uint16 length + bytes), TLV 0x10 use_jumbo_report */
    n = 0;
    qmi[n++] = 0; /* request */
    memcpy(qmi + n, &mTxn, 2);
    n += 2;
    mTxn++;
    uint16_t id = kQmiSnsClientReq, total = 3 + 2 + m + 3 + 1;
    memcpy(qmi + n, &id, 2);
    n += 2;
    memcpy(qmi + n, &total, 2);
    n += 2;
    qmi[n++] = 0x01;
    uint16_t tl = 2 + m;
    memcpy(qmi + n, &tl, 2);
    n += 2;
    uint16_t pl = m;
    memcpy(qmi + n, &pl, 2);
    n += 2;
    memcpy(qmi + n, msg, m);
    n += m;
    qmi[n++] = 0x10;
    tl = 1;
    memcpy(qmi + n, &tl, 2);
    n += 2;
    qmi[n++] = 1;

    struct sockaddr_qrtr sq = {};
    sq.sq_family = AF_QIPCRTR;
    sq.sq_node = mNode;
    sq.sq_port = mPort;
    if (sendto(mSock, qmi, n, 0, (struct sockaddr*)&sq, sizeof(sq)) < 0) {
        ALOGE("send msg %u: %s", msgId, strerror(errno));
        return false;
    }
    return true;
}

Suid Client::lookup(const std::string& type, int timeoutMs) {
    uint8_t req[128];
    size_t n;

    if (type.size() > 64) return {};
    {
        std::lock_guard<std::mutex> lock(mLookupLock);
        mLookups.erase(type);
    }
    n = putBytes(req, 1, type.data(), type.size());
    n += putUint(req + n, 2, 1); /* register_updates */
    n += putUint(req + n, 3, 1); /* default_only */
    if (!sendRequest({kSuidLookupSensor, kSuidLookupSensor}, kMsgSuidReq, req, n, false))
        return {};

    std::unique_lock<std::mutex> lock(mLookupLock);
    mLookupCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [&] { return mLookups.count(type) > 0; });
    auto it = mLookups.find(type);
    return it == mLookups.end() ? Suid{} : it->second;
}

bool Client::enable(const Suid& suid, float rateHz, bool wakeup) {
    uint8_t cfg[8];
    size_t n = 0;

    if (rateHz > 0) {
        uint32_t r;
        memcpy(&r, &rateHz, 4);
        n = putFixed32(cfg, 1, r); /* sns_std_sensor_config.sample_rate */
        return sendRequest(suid, kMsgStdSensorConfig, cfg, n, wakeup);
    }
    return sendRequest(suid, kMsgStdOnChangeConfig, cfg, 0, wakeup);
}

bool Client::disable(const Suid& suid) {
    return sendRequest(suid, kMsgClientDisable, nullptr, 0, false);
}

/* sns_client_event_msg: 1 suid, 2 repeated event{1 msg_id, 2 timestamp, 3 payload} */
void Client::handleEventMsg(const uint8_t* d, size_t len) {
    const uint8_t *p = d, *end = d + len;
    Field f;
    Suid suid;

    while (nextField(&p, end, &f)) {
        if (f.field == 1 && f.wt == 2) {
            suid = parseSuid(f.d, f.len);
            continue;
        }
        if (f.field != 2 || f.wt != 2) continue;

        const uint8_t *q = f.d, *qe = f.d + f.len;
        Field g;
        uint32_t msgId = 0;
        uint64_t ts = 0;
        const uint8_t* payload = nullptr;
        size_t plen = 0;

        while (nextField(&q, qe, &g)) {
            if (g.field == 1) msgId = g.v;
            else if (g.field == 2) ts = g.v;
            else if (g.field == 3 && g.wt == 2) {
                payload = g.d;
                plen = g.len;
            }
        }

        if (msgId == kMsgSuidEvent && payload) {
            /* sns_suid_event: 1 data_type, 2 repeated suid */
            const uint8_t *r = payload, *re = payload + plen;
            Field h;
            std::string type;
            Suid found;

            while (nextField(&r, re, &h)) {
                if (h.field == 1 && h.wt == 2)
                    type.assign((const char*)h.d, h.len);
                else if (h.field == 2 && h.wt == 2 && !found.valid())
                    found = parseSuid(h.d, h.len);
            }
            {
                std::lock_guard<std::mutex> lock(mLookupLock);
                mLookups[type] = found;
            }
            mLookupCv.notify_all();
            continue;
        }

        if (mEventFn) mEventFn(suid, msgId, ticksToBoottimeNs(ts), payload, plen);
    }
}

void Client::rxLoop() {
    static uint8_t buf[65536];

    while (!mStop) {
        /*
         * qrtr_recvmsg() holds the socket lock while it blocks, which would
         * stall every sendto() on this socket, so only recv once readable.
         */
        struct pollfd pfd = {mSock, POLLIN, 0};
        if (poll(&pfd, 1, 500) <= 0) continue;

        ssize_t n = recv(mSock, buf, sizeof(buf), MSG_DONTWAIT);

        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            if (!mStop) ALOGE("recv: %s", strerror(errno));
            break;
        }
        /* indications only; the payload is in TLV 0x02 (uint16 len + data) */
        if (n < 7 || buf[0] != 4) continue;
        for (size_t off = 7; off + 3 <= (size_t)n;) {
            uint8_t t = buf[off];
            uint16_t l;

            memcpy(&l, buf + off + 1, 2);
            if (off + 3 + l > (size_t)n) break;
            if (t == 0x02 && l >= 2) {
                uint16_t pl;

                memcpy(&pl, buf + off + 3, 2);
                if (pl <= l - 2) handleEventMsg(buf + off + 5, pl);
            }
            off += 3 + l;
        }
    }
}

}  // namespace ssc
