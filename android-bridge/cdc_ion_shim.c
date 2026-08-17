// SPDX-License-Identifier: MIT
/*
 * Android VENC creates an ION bookkeeping handle before it asks the supplied
 * ScMemOpsS allocator for buffers. H618 Linux has no ION node; the coherent
 * CMA MemAdapter bridge owns allocation instead. These interposed legacy ION
 * entry points provide only that bookkeeping compatibility.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* A real VE fd satisfies legacy bookkeeping that validates an fd before it
 * delegates allocation to our ScMemOpsS bridge. Treat it as IOMMU-style so
 * that code never issues old ION custom ioctls on that fd. */
__attribute__((visibility("default"))) int CdcIonOpen(void) { return open("/dev/cedar_dev", O_RDWR | O_CLOEXEC); }
__attribute__((visibility("default"))) int CdcIonClose(int fd) { return close(fd); }
__attribute__((visibility("default"))) int CdcIonGetMemType(void) { return 1; }
__attribute__((visibility("default"))) int CdcIonFree(void) { return 0; }

/* CedarX decoder opens /dev/ion directly, unlike VENC's CdcIonOpen path. */
__attribute__((visibility("default"))) int open(const char *path, int flags, ...)
{
    va_list args;
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (path && strcmp(path, "/dev/ion") == 0)
        path = "/dev/cedar_dev";
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
}

/*
 * Old CedarX VENC probes an ION allocator with command 3 (check_h3pro).
 * The Linux CMA bridge deliberately has no ION ioctl ABI.  The only result
 * consumed by that probe is a zero/non-H3 flag, so answer that one private
 * compatibility query locally and send every other request to the kernel.
 * This library is preloaded only by the isolated vendor encoder launcher.
 */
__attribute__((visibility("default"))) int ioctl(int fd, int request, ...)
{
    va_list args;
    uintptr_t argument;
    long result;
    int saved_errno;

    va_start(args, request);
    argument = va_arg(args, uintptr_t);
    va_end(args);
    if (request == 3) {
        if (argument)
            *(int *)(uintptr_t)argument = 0;
        return 0;
    }
    result = syscall(SYS_ioctl, fd, request, argument);
    saved_errno = errno;
    errno = saved_errno;
    return (int)result;
}
