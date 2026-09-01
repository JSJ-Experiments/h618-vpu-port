// SPDX-License-Identifier: MIT
/* Queue the same raw frame repeatedly to measure VE throughput without making
 * v4l2-ctl refill a multi-megabyte buffer for every frame. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct mapped_buffer { void *addr; size_t length; };

static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

static int xioctl(int fd, unsigned long request, void *arg)
{
    int rc;
    do rc = ioctl(fd, request, arg); while (rc < 0 && errno == EINTR);
    return rc;
}

static void set_format(int fd, enum v4l2_buf_type type, uint32_t fourcc,
                       unsigned int width, unsigned int height,
                       unsigned int sizeimage)
{
    struct v4l2_format f = { .type = type };
    f.fmt.pix.width = width;
    f.fmt.pix.height = height;
    f.fmt.pix.pixelformat = fourcc;
    f.fmt.pix.field = V4L2_FIELD_NONE;
    f.fmt.pix.sizeimage = sizeimage;
    if (xioctl(fd, VIDIOC_S_FMT, &f)) die("VIDIOC_S_FMT");
    if (f.fmt.pix.width != width || f.fmt.pix.height != height ||
        f.fmt.pix.pixelformat != fourcc) {
        fprintf(stderr, "driver changed requested format\n");
        exit(EXIT_FAILURE);
    }
}

static struct mapped_buffer request_one(int fd, enum v4l2_buf_type type)
{
    struct v4l2_requestbuffers req = {
        .count = 1, .type = type, .memory = V4L2_MEMORY_MMAP
    };
    struct v4l2_buffer b = {
        .type = type, .memory = V4L2_MEMORY_MMAP,
        .index = 0
    };
    struct mapped_buffer mapped;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) || req.count != 1) die("VIDIOC_REQBUFS");
    if (xioctl(fd, VIDIOC_QUERYBUF, &b)) die("VIDIOC_QUERYBUF");
    mapped.length = b.length;
    mapped.addr = mmap(NULL, mapped.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, b.m.offset);
    if (mapped.addr == MAP_FAILED) die("mmap");
    return mapped;
}

static void queue_one(int fd, enum v4l2_buf_type type, unsigned int bytesused)
{
    struct v4l2_buffer b = {
        .type = type, .memory = V4L2_MEMORY_MMAP,
        .index = 0, .bytesused = bytesused
    };
    if (xioctl(fd, VIDIOC_QBUF, &b)) die("VIDIOC_QBUF");
}

static unsigned int dequeue_one(int fd, enum v4l2_buf_type type)
{
    struct v4l2_buffer b = {
        .type = type, .memory = V4L2_MEMORY_MMAP
    };
    if (xioctl(fd, VIDIOC_DQBUF, &b)) die("VIDIOC_DQBUF");
    return b.bytesused;
}

static void set_control(int fd, uint32_t id, int value)
{
    struct v4l2_control c = { .id = id, .value = value };
    if (xioctl(fd, VIDIOC_S_CTRL, &c) && errno != EINVAL) die("VIDIOC_S_CTRL");
}

int main(int argc, char **argv)
{
    const enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    const enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct mapped_buffer raw, coded;
    struct timespec start, end;
    unsigned int width, height, frames, raw_size, i, total = 0;
	unsigned int jpeg_quality = 90;
	unsigned int coded_format = V4L2_PIX_FMT_H264;
    FILE *output = NULL;
    int fd;
    double seconds;

    if (argc < 5 || argc > 8) {
        fprintf(stderr, "usage: %s /dev/videoN width height frames [output] [h264|jpeg] [jpeg-quality]\n",
                argv[0]);
        return 64;
    }
    width = strtoul(argv[2], NULL, 0);
    height = strtoul(argv[3], NULL, 0);
    frames = strtoul(argv[4], NULL, 0);
    if (!width || !height || !frames || (width & 1) || (height & 1)) return 64;
    if (argc >= 6) {
        output = fopen(argv[5], "wb");
        if (!output) die("fopen output");
    }
	if (argc >= 7) {
		if (!strcmp(argv[6], "jpeg"))
			coded_format = V4L2_PIX_FMT_JPEG;
		else if (strcmp(argv[6], "h264")) {
			fprintf(stderr, "unsupported coded format: %s\n", argv[6]);
			return 64;
		}
	}
	if (argc == 8) {
		jpeg_quality = strtoul(argv[7], NULL, 0);
		if (coded_format != V4L2_PIX_FMT_JPEG || jpeg_quality < 1 ||
		    jpeg_quality > 100)
			return 64;
	}
    raw_size = width * height * 3 / 2;
    fd = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fd < 0) die("open");
    set_format(fd, out_type, V4L2_PIX_FMT_NV12, width, height, raw_size);
    set_format(fd, cap_type, coded_format, width, height, 8 * 1024 * 1024);
    if (coded_format == V4L2_PIX_FMT_H264) {
        set_control(fd, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 20);
        set_control(fd, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 22);
        set_control(fd, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
                    V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
#ifdef V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR
        set_control(fd, V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR, 1);
#endif
    } else {
        set_control(fd, V4L2_CID_JPEG_COMPRESSION_QUALITY, jpeg_quality);
    }
    raw = request_one(fd, out_type);
    coded = request_one(fd, cap_type);
    if (raw.length < raw_size) { fprintf(stderr, "short raw mapping\n"); return 1; }
    memset(raw.addr, 16, width * height);
    memset((char *)raw.addr + width * height, 128, raw_size - width * height);
    queue_one(fd, cap_type, 0);
    queue_one(fd, out_type, raw_size);
    if (xioctl(fd, VIDIOC_STREAMON, (void *)&cap_type)) die("CAP STREAMON");
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (xioctl(fd, VIDIOC_STREAMON, (void *)&out_type)) die("OUT STREAMON");
    for (i = 0; i < frames; i++) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT };
        unsigned int used;
        if (poll(&pfd, 1, 10000) <= 0) die("poll");
        used = dequeue_one(fd, cap_type);
        (void)dequeue_one(fd, out_type);
        if (output && fwrite(coded.addr, 1, used, output) != used)
            die("fwrite output");
        total += used;
        if (i + 1 < frames) {
            queue_one(fd, cap_type, 0);
            queue_one(fd, out_type, raw_size);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("%ux%u: %u frames, %.3f s, %.2f fps, %u coded bytes\n",
           width, height, frames, seconds, frames / seconds, total);
    xioctl(fd, VIDIOC_STREAMOFF, (void *)&out_type);
    xioctl(fd, VIDIOC_STREAMOFF, (void *)&cap_type);
    munmap(raw.addr, raw.length);
    munmap(coded.addr, coded.length);
    if (output && fclose(output)) die("fclose output");
    close(fd);
    return 0;
}
