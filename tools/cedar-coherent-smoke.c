// SPDX-License-Identifier: MIT
/* Post-reflash smoke test; run only while the legacy VE driver owns the VPU. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define IOCTL_ALLOC_COHERENT 0x710
#define IOCTL_FREE_COHERENT  0x711
#define CEDAR_PAGE_SIZE 4096U

struct cedar_coherent_alloc { uint32_t size, handle; uint64_t dma_addr; };

int main(void)
{
    struct cedar_coherent_alloc a = { .size = 1024 * 1024 };
    size_t length = (a.size + CEDAR_PAGE_SIZE - 1) & ~(CEDAR_PAGE_SIZE - 1);
    volatile uint32_t *words;
    int rc = 1;
    int fd = open("/dev/cedar_dev", O_RDWR | O_CLOEXEC);

    if (fd < 0) { perror("open /dev/cedar_dev"); return 1; }
    if (ioctl(fd, IOCTL_ALLOC_COHERENT, &a) < 0) { perror("IOCTL_ALLOC_COHERENT"); goto close_fd; }
    if (!a.handle || !a.dma_addr) {
        fprintf(stderr, "invalid allocation: handle=%u dma=0x%" PRIx64 "\n", a.handle, a.dma_addr);
        goto free_buffer;
    }
    words = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                 (off_t)a.handle * CEDAR_PAGE_SIZE);
    if (words == MAP_FAILED) { perror("mmap"); goto free_buffer; }
    words[0] = 0x12345678U;
    words[length / sizeof(*words) - 1] = 0x87654321U;
    if (words[0] != 0x12345678U || words[length / sizeof(*words) - 1] != 0x87654321U) {
        fprintf(stderr, "coherent mapping readback failed\n");
        munmap((void *)words, length); goto free_buffer;
    }
    printf("coherent CMA OK: size=%u handle=%u dma=0x%" PRIx64 "\n", a.size, a.handle, a.dma_addr);
    munmap((void *)words, length);
    rc = 0;
free_buffer:
    if (ioctl(fd, IOCTL_FREE_COHERENT, &a) < 0) { perror("IOCTL_FREE_COHERENT"); rc = 1; }
close_fd:
    close(fd);
    return rc;
}
