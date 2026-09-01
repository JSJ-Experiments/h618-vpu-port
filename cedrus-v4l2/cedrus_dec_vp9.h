/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CEDRUS_DEC_VP9_H_
#define _CEDRUS_DEC_VP9_H_

#include <media/v4l2-ctrls.h>
#include <media/v4l2-vp9.h>

/* Private working areas used by the H618 AL VP9 engine. */
#define CEDRUS_DEC_VP9_PROB_COUNT_SIZE		0x88000
#define CEDRUS_DEC_VP9_ENTRY_INFO_SIZE		0x1f4000
#define CEDRUS_DEC_VP9_PROBS_OFFSET		0x4000
#define CEDRUS_DEC_VP9_PROBS_SIZE		0xafc

struct cedrus_dec_vp9_context {
	void *prob_count;
	dma_addr_t prob_count_dma;
	void *entry_info;
	dma_addr_t entry_info_dma;
	void *segment_map[2];
	dma_addr_t segment_map_dma[2];
	size_t segment_map_size;
	struct v4l2_vp9_frame_context probability_tables;
	struct v4l2_vp9_frame_context frame_context[V4L2_VP9_NUM_FRAME_CTX];
	u8 frame_context_idx;
};

struct cedrus_dec_vp9_buffer {
	void *mv_col;
	dma_addr_t mv_col_dma;
	size_t mv_col_size;
};

struct cedrus_dec_vp9_job {
	const struct v4l2_ctrl_vp9_frame *frame;
	const struct v4l2_ctrl_vp9_compressed_hdr *compressed_hdr;
};

extern const struct cedrus_engine cedrus_dec_vp9;

#endif
