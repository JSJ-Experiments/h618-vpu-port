// SPDX-License-Identifier: MIT
/*
 * Optional Bionic close(2) interposer used only to capture a successful
 * Android CedarX run's final H618 VE register page.  The stable vendor smoke
 * executable resolves its encoder calls with dlsym(), so an end-of-fd-life
 * snapshot avoids changing that proven ABI merely to observe registers.
 *
 * Set H618_VE_REGISTER_SNAPSHOT=/path/to/output.bin. The first fd which
 * accepts the legacy VE register mapping is copied verbatim (4096 bytes).
 * This is diagnostic-only and must run as root while legacy-ve-session owns
 * the VE node.
 */
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define VE_REGS_BASE 0x01c0e000UL
#define VE_REGS_SIZE 0x1000U
#define IOCTL_WAIT_VE_EN 0x102

static int snapshot_done;
static int (*next_ioctl)(int, int, ...);

static void try_snapshot(int fd)
{
    const char *path;
    void *regs;
    int out;
    ssize_t written;

    if (snapshot_done)
        return;
    path = getenv("H618_VE_REGISTER_SNAPSHOT");
    if (!path || !*path)
        return;
    regs = mmap(NULL, VE_REGS_SIZE, PROT_READ, MAP_SHARED, fd, VE_REGS_BASE);
    if (regs == MAP_FAILED)
        return;
    out = (int)syscall(SYS_openat, AT_FDCWD, path,
                       O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out >= 0) {
        written = (ssize_t)syscall(SYS_write, out, regs, VE_REGS_SIZE);
        (void)syscall(SYS_close, out);
        if (written == VE_REGS_SIZE)
            snapshot_done = 1;
    }
    munmap(regs, VE_REGS_SIZE);
}

__attribute__((visibility("default"))) int close(int fd)
{
    int result;
    int saved_errno;

    try_snapshot(fd);
    result = (int)syscall(SYS_close, fd);
    saved_errno = errno;
    errno = saved_errno;
    return result;
}

/* VE completion is the useful observation point: the vendor has programmed
 * the final H.264 registers, but has not yet freed its bitstream state.  Use
 * RTLD_NEXT so libCdcIonShim still handles its private command-3 emulation. */
__attribute__((visibility("default"))) int ioctl(int fd, int request, ...)
{
    va_list args;
    uintptr_t argument;
    int result;

    va_start(args, request);
    argument = va_arg(args, uintptr_t);
    va_end(args);
    if (!next_ioctl)
        next_ioctl = dlsym(RTLD_NEXT, "ioctl");
    if (!next_ioctl) {
        errno = ENOSYS;
        return -1;
    }
    result = next_ioctl(fd, request, argument);
    if (request == IOCTL_WAIT_VE_EN && result >= 0)
        try_snapshot(fd);
    return result;
}
