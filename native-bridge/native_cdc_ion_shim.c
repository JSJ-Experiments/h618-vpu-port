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
