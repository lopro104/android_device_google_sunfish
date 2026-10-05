// SPDX-License-Identifier: Apache-2.0
/*
 * locget: start a GNSS session on the modem's QMI LOC service over QRTR and
 * print position reports and NMEA. Bring-up tool, no dependencies.
 *
 * Usage: locget [seconds]
 */
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

#define LOC_SERVICE		16
#define LOC_REG_EVENTS		0x0021
#define LOC_START		0x0022
#define LOC_STOP		0x0023
#define LOC_POSITION_IND	0x0024
#define LOC_NMEA_IND		0x0026
#define LOC_ENGINE_STATE_IND	0x002b
#define LOC_FIX_SESSION_IND	0x002c

static int sock;
static struct sockaddr_qrtr loc;
static uint16_t txn = 1;

static int find_service(void)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	struct qrtr_ctrl_pkt pkt;
	struct timeval tv = { 2, 0 };
	int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);

	if (s < 0 || getsockname(s, (void *)&sq, &sl))
		return -1;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = LOC_SERVICE;
	sq.sq_port = QRTR_PORT_CTRL;
	sendto(s, &pkt, sizeof(pkt), 0, (void *)&sq, sizeof(sq));
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	while (recv(s, &pkt, sizeof(pkt), 0) > 0) {
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER || !pkt.server.service)
			break;
		loc.sq_family = AF_QIPCRTR;
		loc.sq_node = pkt.server.node;
		loc.sq_port = pkt.server.port;
		close(s);
		return 0;
	}
	close(s);
	return -1;
}

/* Append a TLV to buf at *n */
static void tlv(uint8_t *buf, size_t *n, uint8_t t, const void *v, uint16_t l)
{
	buf[(*n)++] = t;
	memcpy(buf + *n, &l, 2);
	*n += 2;
	memcpy(buf + *n, v, l);
	*n += l;
}

static int send_req(uint16_t msg, const uint8_t *tlvs, size_t len)
{
	uint8_t buf[512];
	uint16_t l = len;

	buf[0] = 0;
	memcpy(buf + 1, &txn, 2);
	txn++;
	memcpy(buf + 3, &msg, 2);
	memcpy(buf + 5, &l, 2);
	memcpy(buf + 7, tlvs, len);
	return sendto(sock, buf, 7 + len, 0, (void *)&loc, sizeof(loc)) < 0;
}

static const uint8_t *find_tlv(const uint8_t *p, size_t len, uint8_t t,
			       uint16_t *l)
{
	size_t off = 0;

	while (off + 3 <= len) {
		uint16_t tl;

		memcpy(&tl, p + off + 1, 2);
		if (off + 3 + tl > len)
			break;
		if (p[off] == t) {
			*l = tl;
			return p + off + 3;
		}
		off += 3 + tl;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 60;
	uint8_t t[64], buf[8192];
	time_t start = time(NULL);
	size_t n;

	if (find_service()) {
		fprintf(stderr, "LOC service not found\n");
		return 1;
	}
	printf("LOC service on node %u port %u\n", loc.sq_node, loc.sq_port);
	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);

	/* position, satellites, NMEA, engine state, fix session state */
	uint64_t mask = 1 << 0 | 1 << 1 | 1 << 2 | 1 << 7 | 1 << 8;
	uint32_t client = argc > 2 ? atoi(argv[2]) : 1; /* 1 AFW, 2 NFW, 3 privileged */
	n = 0;
	tlv(t, &n, 0x01, &mask, 8);
	tlv(t, &n, 0x10, "locget", 6);
	tlv(t, &n, 0x11, &client, 4);
	send_req(LOC_REG_EVENTS, t, n);

	/* Qualcomm loc API: client revision, then unlock and go standalone */
	uint32_t rev = 2, lock = 1 /* none */, mode = 4 /* standalone */;
	n = 0;
	tlv(t, &n, 0x01, &rev, 4);
	send_req(0x0020, t, n);
	send_req(0x003b, t, 0); /* get engine lock */
	n = 0;
	tlv(t, &n, 0x01, &lock, 4);
	send_req(0x003a, t, n);
	n = 0;
	tlv(t, &n, 0x01, &mode, 4);
	send_req(0x004a, t, n);
	usleep(500000);

	uint8_t session = 1;
	uint32_t recurrence = 1, interval = 1000;
	n = 0;
	tlv(t, &n, 0x01, &session, 1);
	tlv(t, &n, 0x10, &recurrence, 4);
	tlv(t, &n, 0x13, &interval, 4);
	send_req(LOC_START, t, n);

	while (time(NULL) - start < secs) {
		struct timeval tv = { 1, 0 };
		uint16_t msg, mlen, l;
		const uint8_t *v;
		ssize_t r;

		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		r = recv(sock, buf, sizeof(buf) - 1, 0);
		if (r < 7)
			continue;
		memcpy(&msg, buf + 3, 2);
		memcpy(&mlen, buf + 5, 2);
		if (buf[0] == 2) {
			v = find_tlv(buf + 7, mlen, 0x02, &l);
			printf("resp 0x%04x result %u error %u\n", msg,
			       v ? v[0] | v[1] << 8 : 0xffff,
			       v ? v[2] | v[3] << 8 : 0xffff);
			continue;
		}
		if (buf[0] != 4)
			continue;
		switch (msg) {
		case LOC_NMEA_IND:
			v = find_tlv(buf + 7, mlen, 0x01, &l);
			if (v)
				printf("%.*s", l, v);
			break;
		case LOC_POSITION_IND: {
			double lat = 0, lon = 0;
			float acc = 0;
			uint32_t status = 0;

			if ((v = find_tlv(buf + 7, mlen, 0x01, &l)))
				memcpy(&status, v, 4);
			if ((v = find_tlv(buf + 7, mlen, 0x10, &l)))
				memcpy(&lat, v, 8);
			if ((v = find_tlv(buf + 7, mlen, 0x11, &l)))
				memcpy(&lon, v, 8);
			if ((v = find_tlv(buf + 7, mlen, 0x12, &l)))
				memcpy(&acc, v, 4);
			printf("POSITION status %u lat %.6f lon %.6f acc %.1f m\n",
			       status, lat, lon, acc);
			break;
		}
		default: {
			int i;

			printf("ind 0x%04x:", msg);
			for (i = 0; i < mlen && i < 24; i++)
				printf(" %02x", buf[7 + i]);
			printf("\n");
		}
		}
		fflush(stdout);
	}

	n = 0;
	tlv(t, &n, 0x01, &session, 1);
	send_req(LOC_STOP, t, n);
	return 0;
}
