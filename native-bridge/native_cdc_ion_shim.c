#define _GNU_SOURCE
// SPDX-License-Identifier: MIT
/* Native CedarX compatibility for a CMA-backed MemAdapter. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
/* Return MEMORY_NORMAL: all bridge buffers expose contiguous DMA addresses,
 * not IOMMU virtual addresses. The VENC closure only uses this as an address
 * mode selector when its allocator is MemAdapter. */
int CdcIonGetMemType(void) { return 0; }
int CdcIonOpen(void) { return open("/dev/cedar_dev", O_RDWR | O_CLOEXEC); }
int CdcIonClose(int fd) { return close(fd); }
int CdcIonFree(int fd, uintptr_t handle) { (void)fd; (void)handle; return 0; }
/* Android-origin VENC calls this obsolete probe through libcdc_base. */
int ioctl(int fd, unsigned long request, ...) {
    va_list ap; uintptr_t arg; va_start(ap, request); arg=va_arg(ap, uintptr_t); va_end(ap);
    if (request == 3) { if (arg) *(int *)arg=0; return 0; }
    if (getenv("CEDAR_IOCTL_DEBUG")) dprintf(2, "cedar ioctl fd=%d req=0x%lx arg=0x%lx\n", fd, request, (unsigned long)arg);
    int rc = (int)syscall(SYS_ioctl, fd, request, arg);
    if (getenv("CEDAR_IOCTL_DEBUG")) dprintf(2, "cedar ioctl rc=%d\n", rc);
    return rc;
}
/* Newer Allwinner binary components import these logging-key symbols rather
 * than defining them in libcdc_base. Keep them process-local and harmless. */
char CDX_LOG_LEVEL_NAME[] = "CEDARC_LOG_LEVEL";
char CDC_LOG_LEVEL_NAME[] = "CEDARC_LOG_LEVEL";
/* The public Linux base library does not include Android's cedarc.conf parser.
 * Returning zero selects vendor defaults; callers treat this as "not set". */
int CdcGetConfigParamterInt(const char *key, int notfound) {
    (void)key;
    return notfound;
}
/* Vendor libVE emits Android logger records through printf() with a logger
 * context pointer that is not valid in a glibc process. Drop only that record
 * shape; forward ordinary printf calls (including the smoke-test result). */
#include <dlfcn.h>
#include <string.h>
int printf(const char *format, ...) {
    /* The imported CedarX libraries use printf only for Android diagnostics;
     * suppress it rather than interpreting their incompatible varargs. */
    (void)format;
    return 0;
}
/* glibc's fortified logger path bypasses printf, but has the same malformed
 * Android logger record. */
int __printf_chk(int flag, const char *format, ...) {
    /* CedarX's glibc closure uses this only for diagnostic logging. Its
     * Android logger arguments are ABI-incompatible, so logging must remain
     * disabled until a native logger adapter is implemented. */
    (void)flag;
    (void)format;
    return 0;
}
