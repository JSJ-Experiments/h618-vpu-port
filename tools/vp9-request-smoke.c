// SPDX-License-Identifier: MIT
/*
 * One-frame V4L2 stateless VP9 request smoke test for H618 Cedrus.
 *
 * Defaults describe artifacts/vp9/key-320x240.vp9.  Optional arguments:
 *   vp9-request-smoke VIDEO MEDIA FRAME
 *       [width height uhdr chdr q lf tx tile-cols-log2 tile-rows-log2]
 */
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

struct mapped_buffer {
	void *data;
	size_t length;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;
	do {
		ret = ioctl(fd, request, arg);
	} while (ret < 0 && errno == EINTR);
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

static int alloc_map(int fd, enum v4l2_buf_type type,
		     struct mapped_buffer *mapped)
{
	struct v4l2_requestbuffers req = { 0 };
	struct v4l2_buffer buf = { 0 };

	req.count = 1;
	req.type = type;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count != 1) {
		fprintf(stderr, "REQBUFS %u: %s\n", type, strerror(errno));
		return -1;
	}

	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
		fprintf(stderr, "QUERYBUF %u: %s\n", type, strerror(errno));
		return -1;
	}

	mapped->length = buf.length;
	mapped->data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, buf.m.offset);
	if (mapped->data == MAP_FAILED) {
		fprintf(stderr, "mmap %u: %s\n", type, strerror(errno));
		mapped->data = NULL;
		return -1;
	}
	return 0;
}

static int set_control(int fd, int request_fd, uint32_t id,
		       void *data, size_t size)
{
	struct v4l2_ext_control ctrl = { 0 };
	struct v4l2_ext_controls ctrls = { 0 };

	ctrl.id = id;
	ctrl.size = size;
	ctrl.ptr = data;
	ctrls.which = V4L2_CTRL_WHICH_REQUEST_VAL;
	ctrls.request_fd = request_fd;
	ctrls.count = 1;
	ctrls.controls = &ctrl;
	if (xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
		fprintf(stderr, "S_EXT_CTRLS %#x: %s (error_idx=%u)\n",
			id, strerror(errno), ctrls.error_idx);
		return -1;
	}
	return 0;
}

static int queue(int fd, enum v4l2_buf_type type, int request_fd,
		 size_t bytesused, uint64_t timestamp)
{
	struct v4l2_buffer buf = { 0 };

	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	buf.bytesused = bytesused;
	buf.timestamp.tv_sec = timestamp / 1000000000ULL;
	buf.timestamp.tv_usec = (timestamp % 1000000000ULL) / 1000;
	if (request_fd >= 0) {
		buf.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buf.request_fd = request_fd;
	}
	if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
		fprintf(stderr, "QBUF %u: %s\n", type, strerror(errno));
		return -1;
	}
	return 0;
}

static int dequeue(int fd, enum v4l2_buf_type type, size_t *bytesused)
{
	struct v4l2_buffer buf = { 0 };

	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
		fprintf(stderr, "DQBUF %u: %s\n", type, strerror(errno));
		return -1;
	}
	if (bytesused)
		*bytesused = buf.bytesused;
	if (buf.flags & V4L2_BUF_FLAG_ERROR) {
		fprintf(stderr, "buffer %u completed with ERROR\n", type);
		return -1;
	}
	return 0;
}

static uint32_t crc32_bytes(const void *data, size_t length)
{
	const uint8_t *p = data;
	uint32_t crc = ~0U;
	size_t i;
	unsigned int bit;

	for (i = 0; i < length; i++) {
		crc ^= p[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
	}
	return ~crc;
}

int main(int argc, char **argv)
{
	const char *video_path = argc > 1 ? argv[1] : "/dev/video0";
	const char *media_path = argc > 2 ? argv[2] : "/dev/media0";
	const char *frame_path = argc > 3 ? argv[3] :
		"/tmp/key-320x240.vp9";
	unsigned int width = argc > 4 ? strtoul(argv[4], NULL, 0) : 320;
	unsigned int height = argc > 5 ? strtoul(argv[5], NULL, 0) : 240;
	unsigned int uhdr = argc > 6 ? strtoul(argv[6], NULL, 0) : 18;
	unsigned int chdr = argc > 7 ? strtoul(argv[7], NULL, 0) : 170;
	unsigned int q = argc > 8 ? strtoul(argv[8], NULL, 0) : 37;
	unsigned int lf = argc > 9 ? strtoul(argv[9], NULL, 0) : 2;
	unsigned int tx = argc > 10 ? strtoul(argv[10], NULL, 0) : 4;
	unsigned int tile_cols_log2 = argc > 11 ?
		strtoul(argv[11], NULL, 0) : 0;
	unsigned int tile_rows_log2 = argc > 12 ?
		strtoul(argv[12], NULL, 0) : 0;
	struct v4l2_ctrl_vp9_frame frame = { 0 };
	struct v4l2_ctrl_vp9_compressed_hdr compressed = { 0 };
	struct mapped_buffer source = { 0 }, capture = { 0 };
	struct pollfd pfd;
	struct stat st;
	enum v4l2_buf_type type;
	FILE *input = NULL;
	int video = -1, media = -1, request_fd = -1;
	size_t captured = 0;
	int ret = 1;

	if (width < 64 || height < 64 || q > 255 || lf > 63 || tx > 4 ||
	    tile_cols_log2 > 6 || tile_rows_log2 > 2) {
		fprintf(stderr, "invalid frame parameters\n");
		return 64;
	}
	if (stat(frame_path, &st) < 0 || st.st_size <= 0) {
		fprintf(stderr, "stat %s: %s\n", frame_path, strerror(errno));
		return 66;
	}

	video = open(video_path, O_RDWR | O_CLOEXEC);
	media = open(media_path, O_RDWR | O_CLOEXEC);
	if (video < 0 || media < 0) {
		fprintf(stderr, "open devices: %s\n", strerror(errno));
		goto out;
	}

	if (set_format(video, V4L2_BUF_TYPE_VIDEO_OUTPUT,
		       V4L2_PIX_FMT_VP9_FRAME, width, height,
		       st.st_size + 4096) ||
	    set_format(video, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		       V4L2_PIX_FMT_NV12, width, height, 0) ||
	    alloc_map(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, &source) ||
	    alloc_map(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, &capture))
		goto out;

	if ((size_t)st.st_size > source.length) {
		fprintf(stderr, "source mapping too small\n");
		goto out;
	}
	input = fopen(frame_path, "rb");
	if (!input || fread(source.data, 1, st.st_size, input) != (size_t)st.st_size) {
		fprintf(stderr, "read frame: %s\n", strerror(errno));
		goto out;
	}
	fclose(input);
	input = NULL;

	if (xioctl(media, MEDIA_IOC_REQUEST_ALLOC, &request_fd) < 0) {
		fprintf(stderr, "REQUEST_ALLOC: %s\n", strerror(errno));
		goto out;
	}

	frame.lf.ref_deltas[0] = 1;
	frame.lf.ref_deltas[1] = 0;
	frame.lf.ref_deltas[2] = -1;
	frame.lf.ref_deltas[3] = -1;
	frame.lf.level = lf;
	frame.lf.flags = V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED |
			 V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE;
	frame.quant.base_q_idx = q;
	frame.flags = V4L2_VP9_FRAME_FLAG_KEY_FRAME |
		      V4L2_VP9_FRAME_FLAG_SHOW_FRAME |
		      V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX |
		      V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE |
		      V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING |
		      V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING;
	frame.uncompressed_header_size = uhdr;
	frame.compressed_header_size = chdr;
	frame.frame_width_minus_1 = width - 1;
	frame.frame_height_minus_1 = height - 1;
	frame.render_width_minus_1 = width - 1;
	frame.render_height_minus_1 = height - 1;
	frame.reset_frame_context = V4L2_VP9_RESET_FRAME_CTX_ALL;
	frame.profile = 0;
	frame.bit_depth = 8;
	frame.interpolation_filter = V4L2_VP9_INTERP_FILTER_EIGHTTAP;
	frame.reference_mode = V4L2_VP9_REFERENCE_MODE_SINGLE_REFERENCE;
	frame.tile_cols_log2 = tile_cols_log2;
	frame.tile_rows_log2 = tile_rows_log2;
	compressed.tx_mode = tx;
	if (getenv("VP9_SMOKE_COMPRESSED_HDR")) {
		const char *path = getenv("VP9_SMOKE_COMPRESSED_HDR");
		FILE *control = fopen(path, "rb");

		if (!control ||
		    fread(&compressed, 1, sizeof(compressed), control) !=
			    sizeof(compressed)) {
			fprintf(stderr, "read compressed-header control %s: %s\n",
				path, strerror(errno));
			if (control)
				fclose(control);
			goto out;
		}
		fclose(control);
		if (compressed.tx_mode != tx) {
			fprintf(stderr,
				"control tx_mode %u does not match argument %u\n",
				compressed.tx_mode, tx);
			goto out;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (xioctl(video, VIDIOC_STREAMON, &type) < 0) {
		fprintf(stderr, "STREAMON output: %s\n", strerror(errno));
		goto out;
	}
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(video, VIDIOC_STREAMON, &type) < 0) {
		fprintf(stderr, "STREAMON capture: %s\n", strerror(errno));
		goto streamoff;
	}

	if (set_control(video, request_fd, V4L2_CID_STATELESS_VP9_FRAME,
			&frame, sizeof(frame)) ||
	    set_control(video, request_fd,
			V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
			&compressed, sizeof(compressed)) ||
	    queue(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, request_fd, st.st_size,
		  1000000000ULL) ||
	    queue(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, -1, 0, 0))
		goto streamoff;

	if (xioctl(request_fd, MEDIA_REQUEST_IOC_QUEUE, NULL) < 0) {
		fprintf(stderr, "REQUEST_QUEUE: %s\n", strerror(errno));
		goto streamoff;
	}

	pfd.fd = request_fd;
	pfd.events = POLLPRI;
	if (poll(&pfd, 1, 3000) <= 0) {
		fprintf(stderr, "request timeout: %s\n",
			errno ? strerror(errno) : "no completion");
		goto streamoff;
	}

	if (dequeue(video, V4L2_BUF_TYPE_VIDEO_OUTPUT, NULL) ||
	    dequeue(video, V4L2_BUF_TYPE_VIDEO_CAPTURE, &captured))
		goto streamoff;

	if (!captured)
		captured = width * height * 3 / 2;
	if (getenv("VP9_SMOKE_DUMP")) {
		FILE *dump = fopen(getenv("VP9_SMOKE_DUMP"), "wb");

		if (!dump || fwrite(capture.data, 1, captured, dump) != captured) {
			fprintf(stderr, "write capture dump: %s\n",
				strerror(errno));
			if (dump)
				fclose(dump);
			goto streamoff;
		}
		fclose(dump);
	}
	if (getenv("VP9_SMOKE_REFERENCE")) {
		const char *path = getenv("VP9_SMOKE_REFERENCE");
		unsigned char *reference = malloc(captured);
		unsigned long long sad = 0;
		unsigned int max_diff = 0;
		size_t equal = 0, i;
		FILE *ref = fopen(path, "rb");

		if (!reference || !ref ||
		    fread(reference, 1, captured, ref) != captured) {
			fprintf(stderr, "read reference %s: %s\n", path,
				strerror(errno));
		} else {
			const unsigned char *actual = capture.data;

			for (i = 0; i < captured; i++) {
				unsigned int diff = actual[i] > reference[i] ?
					actual[i] - reference[i] :
					reference[i] - actual[i];

				sad += diff;
				if (diff > max_diff)
					max_diff = diff;
				if (!diff)
					equal++;
			}
			printf("reference: sad=%llu equal=%zu/%zu max=%u\n",
			       sad, equal, captured, max_diff);
		}
		if (ref)
			fclose(ref);
		free(reference);
	}
	printf("H618 native VP9 key-frame decode OK: %ux%u, crc32=%08x, bytes=%zu\n",
	       width, height, crc32_bytes(capture.data, captured), captured);
	ret = 0;

streamoff:
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	xioctl(video, VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(video, VIDIOC_STREAMOFF, &type);
out:
	if (input)
		fclose(input);
	if (source.data)
		munmap(source.data, source.length);
	if (capture.data)
		munmap(capture.data, capture.length);
	if (request_fd >= 0)
		close(request_fd);
	if (media >= 0)
		close(media);
	if (video >= 0)
		close(video);
	return ret;
}
