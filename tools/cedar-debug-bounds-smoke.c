// SPDX-License-Identifier: MIT
/* Verify that legacy Cedar debug ioctls cannot overrun their 1 KiB channel. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define IOCTL_SET_PROC_INFO  0x507
#define IOCTL_STOP_PROC_INFO 0x508
#define IOCTL_COPY_PROC_INFO 0x509
#define PROC_INFO_SIZE       1024U

struct ve_proc_info {
	uint8_t channel_id;
	uint32_t proc_info_len;
};

int main(void)
{
	struct ve_proc_info info = { .channel_id = 0,
				     .proc_info_len = PROC_INFO_SIZE + 1 };
	char payload[PROC_INFO_SIZE];
	int fd;

	fd = open("/dev/cedar_dev", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open /dev/cedar_dev");
		return 1;
	}
	errno = 0;
	if (ioctl(fd, IOCTL_SET_PROC_INFO, &info) == 0 || errno != EINVAL) {
		fprintf(stderr, "oversized proc info was not rejected (errno=%d)\n",
			errno);
		return 2;
	}

	memset(payload, 0x5a, sizeof(payload));
	info.proc_info_len = sizeof(payload);
	if (ioctl(fd, IOCTL_SET_PROC_INFO, &info) < 0) {
		perror("bounded IOCTL_SET_PROC_INFO");
		return 3;
	}
	if (ioctl(fd, IOCTL_COPY_PROC_INFO, payload) < 0) {
		perror("bounded IOCTL_COPY_PROC_INFO");
		return 4;
	}
	if (ioctl(fd, IOCTL_STOP_PROC_INFO, 0) < 0) {
		perror("IOCTL_STOP_PROC_INFO");
		return 5;
	}
	puts("debug proc-info bounds OK");
	return 0;
}
