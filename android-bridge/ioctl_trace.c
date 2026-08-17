// SPDX-License-Identifier: MIT
/* Optional diagnostic interposer for private Android/Bionic runs. */
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

__attribute__((visibility("default"))) int ioctl(int fd, int request, ...)
{
    va_list args;
    uintptr_t argument;
    long result;
    int saved_errno;

    va_start(args, request);
    argument = va_arg(args, uintptr_t);
    va_end(args);
    /* Android VENC's check_h3pro() sends legacy command 3 to the allocator
     * handle. The CMA bridge has no ION command set; emulate its successful
     * non-H3 response only for the private diagnostic run. */
    if (request == 3 && getenv("CEDAR_EMULATE_H3PRO")) {
        if (argument)
            *(int *)(uintptr_t)argument = 0;
        fprintf(stderr, "ioctl trace: emulated legacy H3 check on fd=%d\n", fd);
        return 0;
    }
    result = syscall(SYS_ioctl, fd, request, argument);
    saved_errno = errno;
    if (result < 0 || getenv("CEDAR_IOCTL_TRACE"))
        fprintf(stderr, "ioctl trace: fd=%d cmd=0x%lx arg=0x%lx -> %ld (%d)\n",
                fd, (unsigned long)(unsigned int)request, (unsigned long)argument, result, saved_errno);
    errno = saved_errno;
    return (int)result;
}
