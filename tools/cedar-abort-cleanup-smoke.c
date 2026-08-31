// SPDX-License-Identifier: MIT
/* Exercise the legacy driver's abnormal-exit cleanup without starting DMA. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define IOCTL_ENGINE_REQ       0x206
#define IOCTL_GET_LOCK         0x310
#define IOCTL_ALLOC_COHERENT   0x710
#define VE_LOCK_VENC           0x02
#define CEDAR_PAGE_SIZE        4096U

struct cedar_coherent_alloc {
	uint32_t size;
	uint32_t handle;
	uint64_t dma_addr;
};

int main(void)
{
	struct cedar_coherent_alloc alloc = { .size = 1024 * 1024 };
	void *mapping;
	int fd;

	fd = open("/dev/cedar_dev", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open /dev/cedar_dev");
		return 1;
	}
	if (ioctl(fd, IOCTL_ENGINE_REQ, 0) < 0) {
		perror("IOCTL_ENGINE_REQ");
		return 2;
	}
	if (ioctl(fd, IOCTL_GET_LOCK, VE_LOCK_VENC) < 0) {
		perror("IOCTL_GET_LOCK(VENC)");
		return 3;
	}
	if (ioctl(fd, IOCTL_ALLOC_COHERENT, &alloc) < 0) {
		perror("IOCTL_ALLOC_COHERENT");
		return 4;
	}
	mapping = mmap(NULL, alloc.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		       (off_t)alloc.handle * CEDAR_PAGE_SIZE);
	if (mapping == MAP_FAILED) {
		perror("mmap coherent buffer");
		return 5;
	}
	memset(mapping, 0xa5, alloc.size);
	printf("leaving engine ref, VENC lock and CMA handle %u for release()\n",
	       alloc.handle);
	fflush(stdout);

	/* Intentionally skip munmap, FREE_COHERENT, RELEASE_LOCK, ENGINE_REL and
	 * close.  Process teardown must stop the VE before reclaiming the buffer. */
	_exit(0);
}
