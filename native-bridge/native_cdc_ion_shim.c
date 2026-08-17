#define _GNU_SOURCE
// SPDX-License-Identifier: MIT
/* Native CedarX compatibility for a CMA-backed MemAdapter. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
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
    return (int)syscall(SYS_ioctl, fd, request, arg);
}
/* Newer Allwinner binary components import these logging-key symbols rather
 * than defining them in libcdc_base. Keep them process-local and harmless. */
char CDX_LOG_LEVEL_NAME[] = "CEDARC_LOG_LEVEL";
char CDC_LOG_LEVEL_NAME[] = "CEDARC_LOG_LEVEL";
/* The public Linux base library does not include Android's cedarc.conf parser.
 * Returning zero selects vendor defaults; callers treat this as "not set". */
int CdcGetConfigParamterInt(const char *section, const char *key, int *value) {
    (void)section; (void)key; if (value) *value = 0; return -1;
}
/* Vendor libVE emits Android logger records through printf() with a logger
 * context pointer that is not valid in a glibc process. Drop only that record
 * shape; forward ordinary printf calls (including the smoke-test result). */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int printf(const char *format, ...) {
    static int (*real_vprintf)(const char *, va_list);
    va_list ap;
    if (strncmp(format, "%s: %s <%s:%u>:", 16) == 0)
        return 0;
    if (!real_vprintf)
        real_vprintf = dlsym(RTLD_NEXT, "vprintf");
    if (!real_vprintf)
        return -1;
    va_start(ap, format);
    int rc = real_vprintf(format, ap);
    va_end(ap);
    return rc;
}
/* glibc's fortified logger path bypasses printf, but has the same malformed
 * Android logger record. */
int __printf_chk(int flag, const char *format, ...) {
    static int (*real_vfprintf)(FILE *, const char *, va_list);
    va_list ap;
    (void)flag;
    if (strncmp(format, "%s: %s <%s:%u>:", 16) == 0)
        return 0;
    if (!real_vfprintf)
        real_vfprintf = dlsym(RTLD_NEXT, "vfprintf");
    if (!real_vfprintf)
        return -1;
    va_start(ap, format);
    int rc = real_vfprintf(stdout, format, ap);
    va_end(ap);
    return rc;
}
