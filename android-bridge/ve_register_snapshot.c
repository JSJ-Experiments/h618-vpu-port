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
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define VE_REGS_BASE 0x01c0e000UL
#define VE_REGS_SIZE 0x1000U

static int snapshot_done;

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
