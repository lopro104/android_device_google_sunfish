// SPDX-License-Identifier: Apache-2.0
/*
 * qrtrls: list the QMI services announced on QRTR (like qrtr-lookup).
 * Bring-up tool, no dependencies.
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/qrtr.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

int main(void)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	struct qrtr_ctrl_pkt pkt;
	struct timeval tv = { 2, 0 };
	int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);

	if (s < 0 || getsockname(s, (void *)&sq, &sl)) {
		perror("qrtr");
		return 1;
	}
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	sq.sq_port = QRTR_PORT_CTRL;
	if (sendto(s, &pkt, sizeof(pkt), 0, (void *)&sq, sizeof(sq)) < 0) {
		perror("sendto");
		return 1;
	}
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	printf("%8s %8s %6s %6s\n", "service", "version", "node", "port");
	while (recv(s, &pkt, sizeof(pkt), 0) > 0) {
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER || !pkt.server.service)
			break;
		printf("%8u %8u %6u %6u\n", pkt.server.service,
		       pkt.server.instance & 0xff, pkt.server.node,
		       pkt.server.port);
	}
	close(s);
	return 0;
}
