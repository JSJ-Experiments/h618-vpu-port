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
    result = syscall(SYS_ioctl, fd, request, argument);
    saved_errno = errno;
    if (result < 0 || getenv("CEDAR_IOCTL_TRACE"))
        fprintf(stderr, "ioctl trace: fd=%d cmd=0x%lx arg=0x%lx -> %ld (%d)\n",
                fd, (unsigned long)(unsigned int)request, (unsigned long)argument, result, saved_errno);
    errno = saved_errno;
    return (int)result;
}
