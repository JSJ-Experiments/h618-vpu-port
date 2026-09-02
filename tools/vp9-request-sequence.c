// SPDX-License-Identifier: MIT
/* Minimal multi-frame stateless VP9 request client used during H618 bring-up. */
#include <errno.h>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_FRAMES 16

struct mapped_buffer {
	void *data;
	size_t length;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;
	do ret = ioctl(fd, request, arg); while (ret < 0 && errno == EINTR);
	return ret;
}

static int set_format(int fd, enum v4l2_buf_type type, uint32_t format,
		      unsigned int width, unsigned int height,
		      unsigned int sizeimage)
{
	struct v4l2_format f = { 0 };
	f.type = type;
	f.fmt.pix.width = width;
	f.fmt.pix.height = height;
	f.fmt.pix.pixelformat = format;
	f.fmt.pix.field = V4L2_FIELD_NONE;
	f.fmt.pix.sizeimage = sizeimage;
	if (xioctl(fd, VIDIOC_S_FMT, &f) < 0) {
		fprintf(stderr, "S_FMT %u: %s\n", type, strerror(errno));
		return -1;
	}
	fprintf(stderr, "format %u: %c%c%c%c %ux%u stride=%u size=%u\n",
		type, format, format >> 8, format >> 16, format >> 24,
		f.fmt.pix.width, f.fmt.pix.height, f.fmt.pix.bytesperline,
		f.fmt.pix.sizeimage);
	return 0;
}

static int alloc_map(int fd, enum v4l2_buf_type type, unsigned int count,
		     struct mapped_buffer *mapped)
{
	struct v4l2_requestbuffers req = { 0 };
	unsigned int i;

	req.count = count;
	req.type = type;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < count) {
		fprintf(stderr, "REQBUFS %u count=%u: %s\n", type, req.count,
			strerror(errno));
		return -1;
	}
	for (i = 0; i < count; i++) {
		struct v4l2_buffer buf = { 0 };
		buf.type = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			fprintf(stderr, "QUERYBUF %u/%u: %s\n", type, i,
				strerror(errno));
			return -1;
		}
		mapped[i].length = buf.length;
		mapped[i].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
				      MAP_SHARED, fd, buf.m.offset);
		if (mapped[i].data == MAP_FAILED) {
			mapped[i].data = NULL;
			fprintf(stderr, "mmap %u/%u: %s\n", type, i,
				strerror(errno));
			return -1;
		}
	}
	return 0;
}

static int set_controls(int fd, int request_fd,
			struct v4l2_ctrl_vp9_frame *frame,
			struct v4l2_ctrl_vp9_compressed_hdr *probs)
{
	struct v4l2_ext_control ctrl[2] = {
		{ .id = V4L2_CID_STATELESS_VP9_FRAME,
		  .size = sizeof(*frame), .ptr = frame },
		{ .id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
		  .size = sizeof(*probs), .ptr = probs },
	};
	struct v4l2_ext_controls ctrls = {
		.which = V4L2_CTRL_WHICH_REQUEST_VAL,
		.request_fd = request_fd,
		.count = 2,
		.controls = ctrl,
	};
	if (xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
		fprintf(stderr, "S_EXT_CTRLS: %s (error_idx=%u)\n",
			strerror(errno), ctrls.error_idx);
		return -1;
	}
	return 0;
}

static int queue(int fd, enum v4l2_buf_type type, unsigned int index,
		 int request_fd, size_t bytesused, uint64_t timestamp)
{
	struct v4l2_buffer buf = { 0 };
	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = index;
	buf.bytesused = bytesused;
	buf.timestamp.tv_sec = timestamp / 1000000000ULL;
	buf.timestamp.tv_usec = (timestamp % 1000000000ULL) / 1000;
	if (request_fd >= 0) {
		buf.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buf.request_fd = request_fd;
	}
	if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
		fprintf(stderr, "QBUF %u/%u: %s\n", type, index,
			strerror(errno));
		return -1;
	}
	return 0;
}

static int dequeue(int fd, enum v4l2_buf_type type, unsigned int *index,
		   size_t *bytesused, uint64_t *timestamp)
{
	struct v4l2_buffer buf = { 0 };
	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
		fprintf(stderr, "DQBUF %u: %s\n", type, strerror(errno));
		return -1;
	}
	*index = buf.index;
	if (bytesused)
		*bytesused = buf.bytesused;
	if (timestamp)
		*timestamp = (uint64_t)buf.timestamp.tv_sec * 1000000000ULL +
			     (uint64_t)buf.timestamp.tv_usec * 1000ULL;
	if (buf.flags & V4L2_BUF_FLAG_ERROR) {
		fprintf(stderr, "buffer %u/%u completed with ERROR\n", type,
			buf.index);
		return -1;
	}
	return 0;
}

static int read_blob(const char *prefix, unsigned int index, const char *suffix,
		     void *data, size_t size)
{
	char path[4096];
	FILE *file;
	if (snprintf(path, sizeof(path), "%s-%03u.%s", prefix, index, suffix) >=
	    (int)sizeof(path))
		return -1;
	file = fopen(path, "rb");
	if (!file || fread(data, 1, size, file) != size || fgetc(file) != EOF) {
		fprintf(stderr, "read %s: %s\n", path,
			errno ? strerror(errno) : "wrong size");
		if (file)
			fclose(file);
		return -1;
	}
	fclose(file);
	return 0;
}

static int load_frame(const char *prefix, unsigned int index, void *data,
		      size_t capacity, size_t *size)
{
	char path[4096];
	struct stat st;
	FILE *file;
	if (snprintf(path, sizeof(path), "%s-%03u.vp9", prefix, index) >=
	    (int)sizeof(path) || stat(path, &st) || st.st_size <= 0 ||
	    (size_t)st.st_size > capacity) {
		fprintf(stderr, "invalid frame %s: %s\n", path, strerror(errno));
		return -1;
	}
	file = fopen(path, "rb");
	if (!file || fread(data, 1, st.st_size, file) != (size_t)st.st_size) {
		fprintf(stderr, "read %s: %s\n", path, strerror(errno));
		if (file)
			fclose(file);
		return -1;
	}
	fclose(file);
	*size = st.st_size;
	return 0;
}

int main(int argc, char **argv)
{
	const char *video_path, *media_path, *prefix, *output_path;
	unsigned int width, height, frames, i, index;
	struct mapped_buffer source[1] = { 0 }, capture[MAX_FRAMES] = { 0 };
	enum v4l2_buf_type type;
	FILE *output = NULL;
	int video = -1, media = -1, ret = 1;

	if (argc != 8) {
		fprintf(stderr, "usage: %s VIDEO MEDIA PREFIX FRAMES WIDTH HEIGHT OUTPUT\n",
			argv[0]);
		return 64;
	}
	video_path = argv[1]; media_path = argv[2]; prefix = argv[3];
	frames = strtoul(argv[4], NULL, 0);
	width = strtoul(argv[5], NULL, 0); height = strtoul(argv[6], NULL, 0);
	output_path = argv[7];
	if (!frames || frames > MAX_FRAMES || width < 64 || height < 64)
		return 64;
	video = open(video_path, O_RDWR | O_CLOEXEC);
	media = open(media_path, O_RDWR | O_CLOEXEC);
	output = fopen(output_path, "wb");
	if (video < 0 || media < 0 || !output) {
		fprintf(stderr, "open: %s\n", strerror(errno));
		goto out;
	}
	if (set_format(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, V4L2_PIX_FMT_VP9_FRAME,
		       width, height, 2 * 1024 * 1024) ||
	    set_format(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_PIX_FMT_NV12,
		       width, height, 0) ||
	    alloc_map(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, 1, source) ||
	    alloc_map(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, frames, capture))
		goto out;
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (xioctl(video, VIDIOC_STREAMON, &type) < 0)
		goto out;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(video, VIDIOC_STREAMON, &type) < 0)
		goto streamoff;

	for (i = 0; i < frames; i++) {
		struct v4l2_ctrl_vp9_frame frame;
		struct v4l2_ctrl_vp9_compressed_hdr probs;
		struct pollfd pfd;
		uint64_t timestamp, completed_ts;
		size_t frame_size, captured;
		int request_fd = -1;

		if (read_blob(prefix, i, "frame", &frame, sizeof(frame)) ||
		    read_blob(prefix, i, "probs", &probs, sizeof(probs)) ||
		    load_frame(prefix, i, source[0].data, source[0].length,
			       &frame_size) ||
		    xioctl(media, MEDIA_IOC_REQUEST_ALLOC, &request_fd) < 0) {
			fprintf(stderr, "prepare request %u: %s\n", i, strerror(errno));
			if (request_fd >= 0) close(request_fd);
			goto streamoff;
		}
		timestamp = ((uint64_t)i + 1) * 1000000000ULL;
		if (set_controls(video, request_fd, &frame, &probs) ||
		    queue(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, 0, request_fd,
			  frame_size, timestamp) ||
		    queue(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, i, -1, 0, 0) ||
		    xioctl(request_fd, MEDIA_REQUEST_IOC_QUEUE, NULL) < 0) {
			fprintf(stderr, "queue request %u: %s\n", i, strerror(errno));
			close(request_fd);
			goto streamoff;
		}
		pfd.fd = request_fd; pfd.events = POLLPRI; pfd.revents = 0;
		if (poll(&pfd, 1, 4000) <= 0 ||
		    dequeue(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, &index, NULL, NULL) ||
		    dequeue(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, &index, &captured,
			    &completed_ts)) {
			fprintf(stderr, "complete request %u failed\n", i);
			close(request_fd);
			goto streamoff;
		}
		close(request_fd);
		if (!captured)
			captured = width * height * 3 / 2;
		if (index != i || completed_ts != timestamp ||
		    fwrite(capture[index].data, 1, captured, output) != captured) {
			fprintf(stderr, "bad capture frame=%u index=%u ts=%llu/%llu\n",
				i, index, (unsigned long long)completed_ts,
				(unsigned long long)timestamp);
			goto streamoff;
		}
		printf("H618 native VP9 frame %u OK: bytes=%zu capture=%zu ts=%llu\n",
		       i, frame_size, captured, (unsigned long long)completed_ts);
	}
	ret = 0;
streamoff:
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	xioctl(video, VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(video, VIDIOC_STREAMOFF, &type);
out:
	if (output) fclose(output);
	for (i = 0; i < frames && i < MAX_FRAMES; i++)
		if (capture[i].data) munmap(capture[i].data, capture[i].length);
	if (source[0].data) munmap(source[0].data, source[0].length);
	if (media >= 0) close(media);
	if (video >= 0) close(video);
	return ret;
}
