// SPDX-License-Identifier: Apache-2.0
/*
 * ssclist: ask the Qualcomm Snapdragon Sensor Core (SEE, QMI service 400)
 * which sensors it has, by sending SUID lookups for common data types over
 * QRTR. Bring-up tool, no dependencies.
 *
 * Usage: ssclist [timeout_seconds] [data_type...]
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/qrtr.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

#define SSC_SERVICE 400
#define QMI_SNS_CLIENT 0x0020
#define SUID_REQ 512
#define SUID_EVENT 768

static const char *default_types[] = {
	"accel", "gyro", "mag", "pressure", "ambient_light", "proximity",
	"sar", "hall", "ambient_temperature", "humidity", "sensor_temperature",
	"rgb", "device_orient", "amd", "sig_motion", "step_detect", "pedometer",
	"tilt", "gravity", "rotv", "game_rv", "geomag_rv", "touch_gesture",
	"camera_vsync", "offbody_detect", "motion_detect", "accel_cal",
	"gyro_cal", "mag_cal", "registry", "remote_proc_state", "timer",
	"interrupt", "async_com_port", "resampler", "fmv", "cm", "rmd", "smd",
	"distance_bound", "ash", "chre", NULL,
};

/* ---- protobuf helpers ---- */
static size_t put_varint(uint8_t *p, uint64_t v)
{
	size_t n = 0;

	do {
		p[n] = v & 0x7f;
		v >>= 7;
		if (v)
			p[n] |= 0x80;
		n++;
	} while (v);
	return n;
}

static size_t put_tag(uint8_t *p, int field, int wt)
{
	return put_varint(p, (uint64_t)field << 3 | wt);
}

static size_t put_bytes(uint8_t *p, int field, const void *d, size_t len)
{
	size_t n = put_tag(p, field, 2);

	n += put_varint(p + n, len);
	memcpy(p + n, d, len);
	return n + len;
}

static size_t put_fixed64(uint8_t *p, int field, uint64_t v)
{
	size_t n = put_tag(p, field, 1);

	memcpy(p + n, &v, 8);
	return n + 8;
}

static size_t put_fixed32(uint8_t *p, int field, uint32_t v)
{
	size_t n = put_tag(p, field, 5);

	memcpy(p + n, &v, 4);
	return n + 4;
}

static size_t put_uint(uint8_t *p, int field, uint64_t v)
{
	size_t n = put_tag(p, field, 0);

	return n + put_varint(p + n, v);
}

static int get_varint(const uint8_t **p, const uint8_t *end, uint64_t *v)
{
	int shift = 0;

	*v = 0;
	while (*p < end) {
		uint8_t b = *(*p)++;

		*v |= (uint64_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
			return 0;
		shift += 7;
	}
	return -1;
}

/* Iterate fields: returns field number, sets wt, value/ptr/len. */
struct pbf {
	int field, wt;
	uint64_t v;
	const uint8_t *d;
	size_t len;
};

static int pb_next(const uint8_t **p, const uint8_t *end, struct pbf *f)
{
	uint64_t key;

	if (*p >= end || get_varint(p, end, &key))
		return 0;
	f->field = key >> 3;
	f->wt = key & 7;
	switch (f->wt) {
	case 0:
		return !get_varint(p, end, &f->v);
	case 1:
		if (end - *p < 8)
			return 0;
		memcpy(&f->v, *p, 8);
		*p += 8;
		return 1;
	case 5:
		if (end - *p < 4)
			return 0;
		f->v = 0;
		memcpy(&f->v, *p, 4);
		*p += 4;
		return 1;
	case 2:
		if (get_varint(p, end, &f->v) || (uint64_t)(end - *p) < f->v)
			return 0;
		f->d = *p;
		f->len = f->v;
		*p += f->v;
		return 1;
	}
	return 0;
}

/* ---- QRTR / QMI ---- */
static int sock;
static struct sockaddr_qrtr ssc;
static uint16_t txn = 1;

static int find_service(void)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	struct qrtr_ctrl_pkt pkt;
	int s;

	s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	if (getsockname(s, (void *)&sq, &sl)) {
		close(s);
		return -1;
	}
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = SSC_SERVICE;
	sq.sq_port = QRTR_PORT_CTRL;
	if (sendto(s, &pkt, sizeof(pkt), 0, (void *)&sq, sizeof(sq)) < 0) {
		close(s);
		return -1;
	}
	for (;;) {
		struct timeval tv = { 1, 0 };

		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		if (recv(s, &pkt, sizeof(pkt), 0) <= 0)
			break;
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER || !pkt.server.service)
			break;
		if (pkt.server.service == SSC_SERVICE) {
			ssc.sq_family = AF_QIPCRTR;
			ssc.sq_node = pkt.server.node;
			ssc.sq_port = pkt.server.port;
			close(s);
			return 0;
		}
	}
	close(s);
	return -1;
}

static int send_suid_req(const char *type)
{
	uint8_t suidreq[128], body[160], uid[32], cfg[16], msg[512], qmi[600];
	size_t n, b, u, c, m;

	n = put_bytes(suidreq, 1, type, strlen(type));
	n += put_uint(suidreq + n, 2, 1); /* enable_updates */
	n += put_uint(suidreq + n, 3, 1); /* only default */

	b = put_bytes(body, 2, suidreq, n);
	u = put_fixed64(uid, 1, 0xABABABABABABABABULL);
	u += put_fixed64(uid + u, 2, 0xABABABABABABABABULL);
	c = put_uint(cfg, 1, 1); /* processor: APSS */
	c += put_uint(cfg + c, 2, 0);

	m = put_bytes(msg, 1, uid, u);
	m += put_fixed32(msg + m, 2, SUID_REQ);
	m += put_bytes(msg + m, 3, cfg, c);
	m += put_bytes(msg + m, 4, body, b);

	/* QMI header + TLV 0x01 (uint16 length + bytes) + TLV 0x10 (jumbo) */
	n = 0;
	qmi[n++] = 0; /* request */
	memcpy(qmi + n, &txn, 2); n += 2; txn++;
	uint16_t id = QMI_SNS_CLIENT, len = 3 + 2 + m + 3 + 1;
	memcpy(qmi + n, &id, 2); n += 2;
	memcpy(qmi + n, &len, 2); n += 2;
	qmi[n++] = 0x01;
	uint16_t tl = 2 + m;
	memcpy(qmi + n, &tl, 2); n += 2;
	uint16_t pl = m;
	memcpy(qmi + n, &pl, 2); n += 2;
	memcpy(qmi + n, msg, m); n += m;
	qmi[n++] = 0x10;
	tl = 1;
	memcpy(qmi + n, &tl, 2); n += 2;
	qmi[n++] = 1;

	return sendto(sock, qmi, n, 0, (void *)&ssc, sizeof(ssc)) < 0 ? -1 : 0;
}

static void handle_event(const uint8_t *d, size_t len)
{
	const uint8_t *p = d, *end = d + len;
	struct pbf f;

	/* sns_client_event_msg: 1 suid, 2 repeated events{1 msg_id,2 ts,3 payload} */
	while (pb_next(&p, end, &f)) {
		const uint8_t *q, *qe;
		struct pbf g;
		uint32_t msgid = 0;
		const uint8_t *payload = NULL;
		size_t plen = 0;

		if (f.field != 2 || f.wt != 2)
			continue;
		q = f.d;
		qe = f.d + f.len;
		while (pb_next(&q, qe, &g)) {
			if (g.field == 1)
				msgid = g.v;
			else if (g.field == 3 && g.wt == 2) {
				payload = g.d;
				plen = g.len;
			}
		}
		if (msgid != SUID_EVENT || !payload)
			continue;

		/* SscSuidResponse: 1 data_type, 2 repeated uid{1 low,2 high} */
		const uint8_t *r = payload, *re = payload + plen;
		struct pbf h;
		char type[64] = "?";
		int count = 0;

		while (pb_next(&r, re, &h)) {
			if (h.field == 1 && h.wt == 2) {
				snprintf(type, sizeof(type), "%.*s", (int)h.len, h.d);
			} else if (h.field == 2 && h.wt == 2) {
				const uint8_t *s = h.d, *se = h.d + h.len;
				struct pbf k;
				uint64_t lo = 0, hi = 0;

				while (pb_next(&s, se, &k))
					if (k.field == 1)
						lo = k.v;
					else if (k.field == 2)
						hi = k.v;
				printf("  %-20s suid %016llx%016llx\n", type,
				       (unsigned long long)hi, (unsigned long long)lo);
				count++;
			}
		}
		if (!count)
			printf("  %-20s (none)\n", type);
		fflush(stdout);
	}
}

int main(int argc, char **argv)
{
	int timeout = argc > 1 ? atoi(argv[1]) : 15;
	const char **types = argc > 2 ? (const char **)argv + 2 : default_types;
	uint8_t buf[65536];
	time_t start = time(NULL);
	int i;

	while (find_service()) {
		if (time(NULL) - start > timeout) {
			fprintf(stderr, "SSC service %d not found\n", SSC_SERVICE);
			return 1;
		}
		usleep(200000);
	}
	printf("SSC service on node %u port %u\n", ssc.sq_node, ssc.sq_port);

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}
	for (i = 0; types[i]; i++)
		if (send_suid_req(types[i]))
			perror("send");

	while (time(NULL) - start < timeout) {
		struct timeval tv = { 1, 0 };
		ssize_t n;
		size_t off;

		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		n = recv(sock, buf, sizeof(buf), 0);
		if (n < 7)
			continue;
		/* indications: walk TLVs, payload in TLV 0x02 (uint16 len + data) */
		if (buf[0] != 4)
			continue;
		for (off = 7; off + 3 <= (size_t)n;) {
			uint8_t t = buf[off];
			uint16_t l;

			memcpy(&l, buf + off + 1, 2);
			if (off + 3 + l > (size_t)n)
				break;
			if (t == 0x02 && l >= 2) {
				uint16_t pl;

				memcpy(&pl, buf + off + 3, 2);
				if (pl <= l - 2)
					handle_event(buf + off + 5, pl);
			}
			off += 3 + l;
		}
	}
	return 0;
}
