// SPDX-License-Identifier: MIT
/*
 * Android VENC creates an ION bookkeeping handle before it asks the supplied
 * ScMemOpsS allocator for buffers. H618 Linux has no ION node; the coherent
 * CMA MemAdapter bridge owns allocation instead. These interposed legacy ION
 * entry points provide only that bookkeeping compatibility.
 */
__attribute__((visibility("default"))) int CdcIonOpen(void) { return 1; }
__attribute__((visibility("default"))) int CdcIonClose(void) { return 0; }
__attribute__((visibility("default"))) int CdcIonGetMemType(void) { return 0; }
__attribute__((visibility("default"))) int CdcIonFree(void) { return 0; }
