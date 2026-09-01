/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _CEDRUS_VP9_HELPER_H_
#define _CEDRUS_VP9_HELPER_H_

#include <media/v4l2-vp9.h>

extern const struct v4l2_vp9_frame_context cedrus_vp9_default_probs;

void cedrus_vp9_fw_update_probs(
	struct v4l2_vp9_frame_context *probs,
	const struct v4l2_ctrl_vp9_compressed_hdr *deltas,
	const struct v4l2_ctrl_vp9_frame *dec_params);

u8 cedrus_vp9_reset_frame_ctx(
	const struct v4l2_ctrl_vp9_frame *dec_params,
	struct v4l2_vp9_frame_context *frame_context);

void cedrus_vp9_adapt_coef_probs(
	struct v4l2_vp9_frame_context *probs,
	struct v4l2_vp9_frame_symbol_counts *counts,
	bool use_128, bool frame_is_intra);

void cedrus_vp9_adapt_noncoef_probs(
	struct v4l2_vp9_frame_context *probs,
	struct v4l2_vp9_frame_symbol_counts *counts,
	u8 reference_mode, u8 interpolation_filter, u8 tx_mode, u32 flags);

#endif
