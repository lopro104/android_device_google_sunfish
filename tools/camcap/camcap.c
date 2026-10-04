// SPDX-License-Identifier: Apache-2.0
/*
 * camcap: link sensor -> csiphy0 -> csid0 -> vfe0_rdi0 in /dev/media0,
 * propagate the sensor format and capture one raw frame.
 * Usage: camcap [out.raw [sensor [csiphy]]], default imx363 on msm_csiphy0
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ENT 64

struct ent {
	unsigned int id, type, pads, links;
	char name[64];
	unsigned int major, minor;
};

static struct ent ents[MAX_ENT];
static int nents;

static struct ent *find(const char *prefix)
{
	for (int i = 0; i < nents; i++)
		if (!strncmp(ents[i].name, prefix, strlen(prefix)))
			return &ents[i];
	return NULL;
}

static int devpath(struct ent *e, char *buf, size_t n)
{
	/* map major:minor to /dev node via sysfs */
	char p[128];
	snprintf(p, sizeof(p), "/sys/dev/char/%u:%u/uevent", e->major, e->minor);
	FILE *f = fopen(p, "r");
	if (!f)
		return -1;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "DEVNAME=", 8)) {
			line[strcspn(line, "\n")] = 0;
			snprintf(buf, n, "/dev/%s", line + 8);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return -1;
}

static int setup_link(int mfd, struct ent *src, int spad, struct ent *sink, int dpad)
{
	struct media_link_desc l = { 0 };
	l.source.entity = src->id;
	l.source.index = spad;
	l.sink.entity = sink->id;
	l.sink.index = dpad;
	l.flags = MEDIA_LNK_FL_ENABLED;
	if (ioctl(mfd, MEDIA_IOC_SETUP_LINK, &l) < 0) {
		fprintf(stderr, "link %s:%d -> %s:%d failed: %s\n", src->name,
			spad, sink->name, dpad, strerror(errno));
		return -1;
	}
	printf("linked %s:%d -> %s:%d\n", src->name, spad, sink->name, dpad);
	return 0;
}

static int set_fmt(struct ent *e, int pad, struct v4l2_mbus_framefmt *f)
{
	char dev[64];
	if (devpath(e, dev, sizeof(dev)))
		return -1;
	int fd = open(dev, O_RDWR);
	if (fd < 0)
		return -1;
	struct v4l2_subdev_format sf = { .which = V4L2_SUBDEV_FORMAT_ACTIVE,
					 .pad = pad, .format = *f };
	int r = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &sf);
	close(fd);
	printf("fmt %s:%d %ux%u code 0x%x -> %s\n", e->name, pad, f->width,
	       f->height, f->code, r < 0 ? strerror(errno) : "ok");
	return r;
}

static unsigned int pixfmt(unsigned int code)
{
	switch (code) {
	case 0x300f: return V4L2_PIX_FMT_SRGGB10P;	/* SRGGB10_1X10 */
	case 0x300e: return V4L2_PIX_FMT_SGBRG10P;
	case 0x300a: return V4L2_PIX_FMT_SGRBG10P;
	case 0x3007: return V4L2_PIX_FMT_SBGGR10P;
	default: return 0;
	}
}

int main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "/data/local/tmp/frame.raw";
	int mfd = open("/dev/media0", O_RDWR);
	if (mfd < 0) {
		perror("media0");
		return 1;
	}

	struct media_entity_desc d = { .id = MEDIA_ENT_ID_FLAG_NEXT };
	while (nents < MAX_ENT && !ioctl(mfd, MEDIA_IOC_ENUM_ENTITIES, &d)) {
		struct ent *e = &ents[nents++];
		e->id = d.id;
		e->type = d.type;
		e->pads = d.pads;
		e->major = d.dev.major;
		e->minor = d.dev.minor;
		snprintf(e->name, sizeof(e->name), "%s", d.name);
		d.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}

	const char *sname = argc > 2 ? argv[2] : "imx363";
	const char *pname = argc > 3 ? argv[3] : "msm_csiphy0";
	struct ent *sensor = find(sname), *phy = find(pname),
		   *csid = find("msm_csid0"), *rdi = find("msm_vfe0_rdi0"),
		   *vid = find("msm_vfe0_video0");
	if (!sensor || !phy || !csid || !rdi || !vid) {
		fprintf(stderr, "missing entity (sensor=%p phy=%p csid=%p rdi=%p vid=%p)\n",
			sensor, phy, csid, rdi, vid);
		for (int i = 0; i < nents; i++)
			fprintf(stderr, "  %s\n", ents[i].name);
		return 1;
	}

	/* drop links a previous user left from other csiphys into csid0 */
	for (int i = 0; i < nents; i++) {
		if (strncmp(ents[i].name, "msm_csiphy", 10) || &ents[i] == phy)
			continue;
		struct media_link_desc l = { 0 };
		l.source.entity = ents[i].id;
		l.source.index = 1;
		l.sink.entity = csid->id;
		l.sink.index = 0;
		ioctl(mfd, MEDIA_IOC_SETUP_LINK, &l);
	}

	/* sensor:0 -> csiphyN:0, csiphyN:1 -> csid0:0, csid0:1 -> rdi0:0 */
	setup_link(mfd, sensor, 0, phy, 0);
	setup_link(mfd, phy, 1, csid, 0);
	setup_link(mfd, csid, 1, rdi, 0);

	char sdev[64];
	devpath(sensor, sdev, sizeof(sdev));
	int sfd = open(sdev, O_RDWR);
	struct v4l2_subdev_format sf = { .which = V4L2_SUBDEV_FORMAT_ACTIVE };
	if (sfd < 0 || ioctl(sfd, VIDIOC_SUBDEV_G_FMT, &sf) < 0) {
		perror("sensor G_FMT");
		return 1;
	}
	/* No AE without a HAL: push exposure to max, gain to mid-range */
	unsigned int cids[2] = { V4L2_CID_EXPOSURE, V4L2_CID_ANALOGUE_GAIN };
	for (int i = 0; i < 2; i++) {
		struct v4l2_queryctrl q = { .id = cids[i] };
		if (ioctl(sfd, VIDIOC_QUERYCTRL, &q) < 0)
			continue;
		struct v4l2_control c = { .id = cids[i],
			.value = i ? q.minimum + (q.maximum - q.minimum) / 2
				   : q.maximum };
		int r = ioctl(sfd, VIDIOC_S_CTRL, &c);
		printf("%s: range %d..%d set %d -> %s\n", q.name, q.minimum,
		       q.maximum, c.value, r < 0 ? strerror(errno) : "ok");
	}
	close(sfd);
	struct v4l2_mbus_framefmt f = sf.format;
	printf("sensor format %ux%u code 0x%x\n", f.width, f.height, f.code);

	set_fmt(phy, 0, &f);
	set_fmt(csid, 0, &f);
	set_fmt(rdi, 0, &f);

	char vdev[64];
	devpath(vid, vdev, sizeof(vdev));
	int vfd = open(vdev, O_RDWR);
	if (vfd < 0) {
		perror(vdev);
		return 1;
	}
	struct v4l2_format vf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE };
	vf.fmt.pix_mp.width = f.width;
	vf.fmt.pix_mp.height = f.height;
	vf.fmt.pix_mp.pixelformat = pixfmt(f.code);
	vf.fmt.pix_mp.num_planes = 1;
	if (ioctl(vfd, VIDIOC_S_FMT, &vf) < 0) {
		perror("S_FMT");
		return 1;
	}
	printf("video %s: %ux%u plane size %u\n", vdev, vf.fmt.pix_mp.width,
	       vf.fmt.pix_mp.height, vf.fmt.pix_mp.plane_fmt[0].sizeimage);

	struct v4l2_requestbuffers rb = { .count = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = V4L2_MEMORY_MMAP };
	if (ioctl(vfd, VIDIOC_REQBUFS, &rb) < 0) {
		perror("REQBUFS");
		return 1;
	}
	void *mem[2];
	size_t len[2];
	for (unsigned int i = 0; i < rb.count; i++) {
		struct v4l2_plane pl[1] = { 0 };
		struct v4l2_buffer b = { .index = i, .type = rb.type,
			.memory = V4L2_MEMORY_MMAP, .m.planes = pl, .length = 1 };
		ioctl(vfd, VIDIOC_QUERYBUF, &b);
		len[i] = pl[0].length;
		mem[i] = mmap(NULL, len[i], PROT_READ | PROT_WRITE, MAP_SHARED,
			      vfd, pl[0].m.mem_offset);
		ioctl(vfd, VIDIOC_QBUF, &b);
	}
	int type = rb.type;
	if (ioctl(vfd, VIDIOC_STREAMON, &type) < 0) {
		perror("STREAMON");
		return 1;
	}
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(vfd, &fds);
	struct timeval tv = { .tv_sec = 5 };
	if (select(vfd + 1, &fds, NULL, NULL, &tv) <= 0) {
		fprintf(stderr, "no frame within 5 s\n");
		ioctl(vfd, VIDIOC_STREAMOFF, &type);
		return 2;
	}
	struct v4l2_plane pl[1] = { 0 };
	struct v4l2_buffer b = { .type = rb.type, .memory = V4L2_MEMORY_MMAP,
				 .m.planes = pl, .length = 1 };
	if (ioctl(vfd, VIDIOC_DQBUF, &b) < 0) {
		perror("DQBUF");
		return 1;
	}
	unsigned char *p = mem[b.index];
	size_t used = pl[0].bytesused, nz = 0;
	for (size_t i = 0; i < used; i++)
		nz += p[i] != 0;
	printf("got frame: %zu bytes, %zu non-zero\n", used, nz);
	FILE *o = fopen(out, "wb");
	if (o) {
		fwrite(p, 1, used, o);
		fclose(o);
		printf("wrote %s\n", out);
	}
	ioctl(vfd, VIDIOC_STREAMOFF, &type);
	return 0;
}
