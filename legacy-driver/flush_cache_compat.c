// SPDX-License-Identifier: GPL-2.0-only
/*
 * The Android 5.4 source supplied a 32-bit ARM cache-maintenance assembly
 * helper. H618 Orange Pi OS is arm64, so that assembly cannot assemble here.
 *
 * This compatibility placeholder deliberately makes the old user-virtual
 * cache-flush ioctl unavailable as a hardware-validation prerequisite. It
 * permits compilation of the rest of the driver, exposing the real 5.4->6.1
 * API work. It must be replaced by DMA-BUF based synchronisation before this
 * module can be deployed.
 */
#include <linux/types.h>

void cedar_dma_flush_range(const void *start, size_t size)
{
	(void)start;
	(void)size;
}
