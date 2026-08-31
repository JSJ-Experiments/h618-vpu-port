/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CEDRUS_DEC_VP9_H_
#define _CEDRUS_DEC_VP9_H_

#include <media/v4l2-ctrls.h>

/* Private working areas used by the H618 AL VP9 engine. */
#define CEDRUS_DEC_VP9_PROB_COUNT_SIZE		0x88000
#define CEDRUS_DEC_VP9_ENTRY_INFO_SIZE		0x1f4000

struct cedrus_dec_vp9_context {
	void *prob_count;
	dma_addr_t prob_count_dma;
	void *entry_info;
	dma_addr_t entry_info_dma;
	void *segment_map[2];
	dma_addr_t segment_map_dma[2];
	size_t segment_map_size;
};

struct cedrus_dec_vp9_job {
	const struct v4l2_ctrl_vp9_frame *frame;
	const struct v4l2_ctrl_vp9_compressed_hdr *compressed_hdr;
};

/* Compiled for review and incremental implementation, but deliberately absent
 * from cedrus_dec_engines[] until register programming is complete and safely
 * validated on H618 hardware. */
extern const struct cedrus_engine cedrus_dec_vp9;

#endif
