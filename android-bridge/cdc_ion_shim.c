// SPDX-License-Identifier: MIT
/*
 * Android VENC creates an ION bookkeeping handle before it asks the supplied
 * ScMemOpsS allocator for buffers. H618 Linux has no ION node; the coherent
 * CMA MemAdapter bridge owns allocation instead. These interposed legacy ION
 * entry points provide only that bookkeeping compatibility.
 */
#include <fcntl.h>
#include <unistd.h>

/* A real VE fd satisfies legacy bookkeeping that validates an fd before it
 * delegates allocation to our ScMemOpsS bridge. Treat it as IOMMU-style so
 * that code never issues old ION custom ioctls on that fd. */
__attribute__((visibility("default"))) int CdcIonOpen(void) { return open("/dev/cedar_dev", O_RDWR | O_CLOEXEC); }
__attribute__((visibility("default"))) int CdcIonClose(int fd) { return close(fd); }
__attribute__((visibility("default"))) int CdcIonGetMemType(void) { return 1; }
__attribute__((visibility("default"))) int CdcIonFree(void) { return 0; }
