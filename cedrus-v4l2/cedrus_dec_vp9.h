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
#define CEDRUS_DEC_VP9_COUNTS_OFFSET		0x4b00

struct cedrus_vp9_frame_counts {
	/* Exact 0x3398-byte hardware order recovered from vp9_update_counts(). */
	u32 tx8p[2][2];
	u32 tx16p[2][3];
	u32 tx32p[2][4];
	u32 eob_branch[4][2][2][6][6];
	u32 coeff[4][2][2][6][6][4];
	u32 skip[3][2];
	u32 inter_mode[7][4];
	u32 switchable_interp[4][3];
	u32 intra_inter[4][2];
	u32 comp_inter[5][2];
	u32 comp_ref[5][2];
	u32 single_ref[5][2][2];
	u32 y_mode[4][10];
	u32 uv_mode[10][10];
	u32 partition[16][4];
	u32 mv_joint[4];
	u32 mv_sign_1[2];
	u32 mv_class0_1[2];
	u32 mv_class0_hp_1[2];
	u32 mv_hp_1[2];
	u32 mv_classes_1[11];
	u32 mv_bits_1[10][2];
	u32 mv_class0_fp_1[2][4];
	u32 mv_fp_1[4];
	u32 mv_sign_0[2];
	u32 mv_class0_0[2];
	u32 mv_class0_hp_0[2];
	u32 mv_hp_0[2];
	u32 mv_classes_0[11];
	u32 mv_bits_0[10][2];
	u32 mv_class0_fp_0[2][4];
	u32 mv_fp_0[4];
};

struct cedrus_dec_vp9_context {
	void *prob_count;
	dma_addr_t prob_count_dma;
	void *entry_info;
	dma_addr_t entry_info_dma;
	void *segment_map[2];
	dma_addr_t segment_map_dma[2];
	size_t segment_map_size;
	void *mv_col;
	dma_addr_t mv_col_dma;
	size_t mv_col_size;
	struct v4l2_vp9_frame_context probability_tables;
	struct v4l2_vp9_frame_context frame_context[V4L2_VP9_NUM_FRAME_CTX];
	struct v4l2_vp9_frame_symbol_counts counts;
	u32 tx16p[2][4];
	struct {
		u32 sign[2][2];
		u32 classes[2][11];
		u32 class0[2][2];
		u32 bits[2][10][2];
		u32 class0_fp[2][2][4];
		u32 fp[2][4];
		u32 class0_hp[2][2];
		u32 hp[2][2];
	} mv_counts;
	u8 frame_context_idx;
	u32 previous_width;
	u32 previous_height;
	bool previous_valid;
	bool previous_show_frame;
	bool previous_intra_only;
	bool previous_key_frame;
};

struct cedrus_dec_vp9_buffer {
	u32 width;
	u32 height;
	bool valid;
};

struct cedrus_dec_vp9_job {
	const struct v4l2_ctrl_vp9_frame *frame;
	const struct v4l2_ctrl_vp9_compressed_hdr *compressed_hdr;
};

extern const struct cedrus_engine cedrus_dec_vp9;

#endif
