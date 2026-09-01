// SPDX-License-Identifier: GPL-2.0
/*
 * H618 native VP9 decoder.
 *
 * Register programming and private-buffer geometry were recovered from
 * Allwinner's Android libawvp9HwAL.so.  The first enabled path is intentionally
 * restricted to 8-bit profile-0 key frames while inter-frame probability,
 * reference and segmentation state is brought up independently.
 */

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#include "cedrus.h"
#include "cedrus_context.h"
#include "cedrus_dec.h"
#include "cedrus_dec_vp9.h"
#include "cedrus_dec_vp9_quant.h"
#include "cedrus_engine.h"
#include "cedrus_regs.h"
#include "cedrus_vp9_helper.h"

#define CEDRUS_VP9_HDR_KEY_BASE		0x84001000
#define CEDRUS_VP9_SRAM_DEQUANT		0
#define CEDRUS_VP9_SRAM_LOOP_FILTER	0x100

static bool debug_vp9_regs;
module_param(debug_vp9_regs, bool, 0644);
MODULE_PARM_DESC(debug_vp9_regs,
		 "log the H618 VP9 register configuration before each trigger");

static void cedrus_vp9_pack_word(u8 **dst, const u8 *src, size_t size)
{
	memcpy(*dst, src, size);
	*dst += sizeof(u32);
}

/*
 * Convert the standardized V4L2 probability context to the H618 entropy
 * front-end's 0xafc-byte image.  Most probability nodes occupy the low three
 * bytes of a 32-bit word.  The motion-vector contexts use a component-reversed
 * layout recovered from Vp9GetEntrypointOffset() in the Android HAL.
 */
static void cedrus_vp9_pack_probs(void *buffer,
				  const struct v4l2_vp9_frame_context *p)
{
	u8 *base = buffer;
	u8 *dst = base;
	unsigned int i, j, k, l, m;

	memset(base, 0, CEDRUS_DEC_VP9_PROBS_SIZE);

	dst[0] = p->tx8[0][0];
	dst[1] = p->tx8[1][0];
	dst += 4;
	cedrus_vp9_pack_word(&dst, p->tx16[0], 2);
	cedrus_vp9_pack_word(&dst, p->tx16[1], 2);
	cedrus_vp9_pack_word(&dst, p->tx32[0], 3);
	cedrus_vp9_pack_word(&dst, p->tx32[1], 3);

	for (i = 0; i < ARRAY_SIZE(p->coef); i++)
		for (j = 0; j < ARRAY_SIZE(p->coef[0]); j++)
			for (k = 0; k < ARRAY_SIZE(p->coef[0][0]); k++)
				for (l = 0; l < ARRAY_SIZE(p->coef[0][0][0]); l++)
					for (m = 0; m < ARRAY_SIZE(p->coef[0][0][0][0]); m++)
						cedrus_vp9_pack_word(&dst,
							p->coef[i][j][k][l][m], 3);

	cedrus_vp9_pack_word(&dst, p->skip, 3);
	for (i = 0; i < ARRAY_SIZE(p->inter_mode); i++)
		cedrus_vp9_pack_word(&dst, p->inter_mode[i], 3);
	for (i = 0; i < ARRAY_SIZE(p->interp_filter); i++)
		cedrus_vp9_pack_word(&dst, p->interp_filter[i], 2);
	cedrus_vp9_pack_word(&dst, p->is_inter, 4);

	memcpy(dst, p->comp_mode, sizeof(p->comp_mode));
	dst += 8;
	memcpy(dst, p->comp_ref, sizeof(p->comp_ref));
	dst += 8;
	for (i = 0; i < ARRAY_SIZE(p->single_ref); i++)
		cedrus_vp9_pack_word(&dst, p->single_ref[i], 2);
	dst += 4;

	for (i = 0; i < ARRAY_SIZE(p->y_mode); i++) {
		memcpy(dst, p->y_mode[i], sizeof(p->y_mode[i]));
		dst += 16;
	}
	for (i = 0; i < ARRAY_SIZE(p->uv_mode); i++) {
		memcpy(dst, p->uv_mode[i], sizeof(p->uv_mode[i]));
		dst += 16;
	}
	for (i = 0; i < ARRAY_SIZE(p->partition); i++)
		cedrus_vp9_pack_word(&dst, p->partition[i], 3);

	cedrus_vp9_pack_word(&dst, p->mv.joint, 3);
	for (i = 0; i < ARRAY_SIZE(p->mv.sign); i++) {
		u8 *mv = base + 0xa94 + (1 - i) * 0x30;

		mv[0] = p->mv.sign[i];
		mv[1] = p->mv.class0_bit[i];
		mv[2] = p->mv.class0_hp[i];
		mv[3] = p->mv.hp[i];
		memcpy(mv + 0x04, p->mv.class0_fr[i][0], 3);
		memcpy(mv + 0x08, p->mv.class0_fr[i][1], 3);
		memcpy(mv + 0x0c, p->mv.classes[i], 10);
		memcpy(mv + 0x1c, p->mv.bits[i], 10);
		memcpy(mv + 0x28, p->mv.fr[i], 3);
	}

	WARN_ON(dst != base + 0xa94);
}

static int cedrus_vp9_clamp_q(int q)
{
	return clamp_t(int, q, 0, 255);
}

static int cedrus_vp9_segment_value(const struct v4l2_vp9_segmentation *seg,
				    unsigned int segment,
				    unsigned int feature, int base)
{
	int value;

	if (!(seg->flags & V4L2_VP9_SEGMENTATION_FLAG_ENABLED) ||
	    !(seg->feature_enabled[segment] &
	      V4L2_VP9_SEGMENT_FEATURE_ENABLED(feature)))
		return base;

	value = seg->feature_data[segment][feature];
	if (!(seg->flags &
	      V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE))
		value += base;

	return value;
}

static void cedrus_vp9_sram_write(struct cedrus_device *dev, u32 offset,
				  const u32 *values, unsigned int count)
{
	unsigned int i;

	/*
	 * The data port auto-increments.  It must never be read while VP9 is
	 * active: the H618 interconnect can remain locked after a timed-out job.
	 */
	cedrus_write(dev, VE_DEC_VP9_SRAM_OFFSET, offset);
	for (i = 0; i < count; i++)
		cedrus_write(dev, VE_DEC_VP9_SRAM_DATA, values[i]);
}

static void cedrus_vp9_dequant_write(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	const struct v4l2_ctrl_vp9_frame *frame =
		((struct cedrus_dec_vp9_job *)ctx->engine_job)->frame;
	const struct v4l2_vp9_quantization *quant = &frame->quant;
	u32 y[8], uv[8];
	unsigned int segment;

	memset(y, 0, sizeof(y));
	memset(uv, 0, sizeof(uv));

	/*
	 * CedarX only populates segment zero when segmentation is disabled.  The
	 * VP9 block does not mirror segment zero internally, so retain the exact
	 * SRAM image even though a conforming key frame should only select ID 0.
	 */
	for (segment = 0;
	     segment < ((frame->seg.flags &
			 V4L2_VP9_SEGMENTATION_FLAG_ENABLED) ? 8 : 1);
	     segment++) {
		int q = cedrus_vp9_segment_value(&frame->seg, segment,
						V4L2_VP9_SEG_LVL_ALT_Q,
						quant->base_q_idx);
		int y_dc_q = cedrus_vp9_clamp_q(q + quant->delta_q_y_dc);
		int uv_dc_q = cedrus_vp9_clamp_q(q + quant->delta_q_uv_dc);
		int uv_ac_q = cedrus_vp9_clamp_q(q + quant->delta_q_uv_ac);

		q = cedrus_vp9_clamp_q(q);
		y[segment] = cedrus_vp9_dc_qlookup[y_dc_q] |
			     (cedrus_vp9_ac_qlookup[q] << 16);
		uv[segment] = cedrus_vp9_dc_qlookup[uv_dc_q] |
			      (cedrus_vp9_ac_qlookup[uv_ac_q] << 16);
	}

	cedrus_vp9_sram_write(dev, CEDRUS_VP9_SRAM_DEQUANT, y,
			      ARRAY_SIZE(y));
	cedrus_vp9_sram_write(dev, CEDRUS_VP9_SRAM_DEQUANT + sizeof(y), uv,
			      ARRAY_SIZE(uv));
}

static u8 cedrus_vp9_filter_level(int level)
{
	return clamp_t(int, level, 0, 63);
}

static void cedrus_vp9_loop_filter_write(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	const struct v4l2_ctrl_vp9_frame *frame =
		((struct cedrus_dec_vp9_job *)ctx->engine_job)->frame;
	const struct v4l2_vp9_loop_filter *lf = &frame->lf;
	u32 values[16];
	unsigned int segment;

	for (segment = 0; segment < 8; segment++) {
		int level = cedrus_vp9_segment_value(&frame->seg, segment,
						    V4L2_VP9_SEG_LVL_ALT_L,
						    lf->level);
		u8 intra, last0, last1, golden0, golden1, alt0, alt1;

		level = cedrus_vp9_filter_level(level);
		if (lf->flags & V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED) {
			int scale = 1 << (level >> 5);

			intra = cedrus_vp9_filter_level(level +
						lf->ref_deltas[0] * scale);
			last0 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[1] * scale +
						lf->mode_deltas[0] * scale);
			last1 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[1] * scale +
						lf->mode_deltas[1] * scale);
			golden0 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[2] * scale +
						lf->mode_deltas[0] * scale);
			golden1 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[2] * scale +
						lf->mode_deltas[1] * scale);
			alt0 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[3] * scale +
						lf->mode_deltas[0] * scale);
			alt1 = cedrus_vp9_filter_level(level +
						lf->ref_deltas[3] * scale +
						lf->mode_deltas[1] * scale);
		} else {
			intra = last0 = last1 = golden0 = golden1 =
				alt0 = alt1 = level;
		}

		values[segment * 2] = intra | (last0 << 16) | (last1 << 24);
		values[segment * 2 + 1] = golden0 | (golden1 << 8) |
					  (alt0 << 16) | (alt1 << 24);
	}

	cedrus_vp9_sram_write(dev, CEDRUS_VP9_SRAM_LOOP_FILTER, values,
			      ARRAY_SIZE(values));
}

static void cedrus_dec_vp9_buffer_cleanup(struct cedrus_context *ctx,
					  struct cedrus_buffer *buffer)
{
	struct cedrus_dec_vp9_buffer *vp9_buffer = buffer->engine_buffer;
	struct device *dev = ctx->proc->dev->dev;

	if (!vp9_buffer->mv_col_size)
		return;

	dma_free_attrs(dev, vp9_buffer->mv_col_size, vp9_buffer->mv_col,
		       vp9_buffer->mv_col_dma, DMA_ATTR_NO_KERNEL_MAPPING);
	vp9_buffer->mv_col = NULL;
	vp9_buffer->mv_col_size = 0;
}

static int cedrus_dec_vp9_buffer_prepare(struct cedrus_context *ctx,
					 struct cedrus_dec_vp9_buffer *buffer,
					 unsigned int width,
					 unsigned int height)
{
	struct device *dev = ctx->proc->dev->dev;
	size_t rows, cols, blocks, bytes;

	if (buffer->mv_col_size)
		return 0;

	/*
	 * This is the exact H618 CedarX Vp9CreateFbmBuffer geometry:
	 * ALIGN(16 KiB + padded_8x8_blocks * 5 * 128, 1 KiB).
	 */
	rows = (15 + DIV_ROUND_UP(height, 8)) >> 3;
	cols = (15 + DIV_ROUND_UP(width, 8)) >> 3;
	if (check_mul_overflow(rows, cols, &blocks) ||
	    check_mul_overflow(blocks, (size_t)(5 * 128), &bytes) ||
	    check_add_overflow(bytes, (size_t)SZ_16K, &bytes))
		return -EOVERFLOW;

	bytes = ALIGN(bytes, SZ_1K);
	buffer->mv_col = dma_alloc_attrs(dev, bytes, &buffer->mv_col_dma,
					 GFP_KERNEL,
					 DMA_ATTR_NO_KERNEL_MAPPING);
	if (!buffer->mv_col)
		return -ENOMEM;

	buffer->mv_col_size = bytes;
	return 0;
}

static void cedrus_dec_vp9_cleanup(struct cedrus_context *ctx)
{
	struct cedrus_dec_vp9_context *vp9 = ctx->engine_ctx;
	struct device *dev = ctx->proc->dev->dev;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vp9->segment_map); i++) {
		if (!vp9->segment_map[i])
			continue;

		dma_free_coherent(dev, vp9->segment_map_size,
				  vp9->segment_map[i], vp9->segment_map_dma[i]);
		vp9->segment_map[i] = NULL;
	}

	if (vp9->entry_info) {
		dma_free_coherent(dev, CEDRUS_DEC_VP9_ENTRY_INFO_SIZE,
				  vp9->entry_info, vp9->entry_info_dma);
		vp9->entry_info = NULL;
	}

	if (vp9->prob_count) {
		dma_free_coherent(dev, CEDRUS_DEC_VP9_PROB_COUNT_SIZE,
				  vp9->prob_count, vp9->prob_count_dma);
		vp9->prob_count = NULL;
	}

	vp9->segment_map_size = 0;
}

static int cedrus_dec_vp9_setup(struct cedrus_context *ctx)
{
	struct cedrus_dec_vp9_context *vp9 = ctx->engine_ctx;
	struct v4l2_pix_format *coded = &ctx->v4l2.format_coded.fmt.pix;
	struct device *dev = ctx->proc->dev->dev;
	size_t superblocks;
	unsigned int i;
	int ret = -ENOMEM;

	vp9->prob_count = dma_alloc_coherent(dev,
					     CEDRUS_DEC_VP9_PROB_COUNT_SIZE,
					     &vp9->prob_count_dma, GFP_KERNEL);
	if (!vp9->prob_count)
		return -ENOMEM;

	vp9->entry_info = dma_alloc_coherent(dev,
					     CEDRUS_DEC_VP9_ENTRY_INFO_SIZE,
					     &vp9->entry_info_dma, GFP_KERNEL);
	if (!vp9->entry_info)
		goto error;

	if (check_mul_overflow((size_t)DIV_ROUND_UP(coded->width, 64),
			       (size_t)DIV_ROUND_UP(coded->height, 64),
			       &superblocks) ||
	    check_mul_overflow(superblocks, (size_t)32,
			       &vp9->segment_map_size)) {
		ret = -EOVERFLOW;
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(vp9->segment_map); i++) {
		vp9->segment_map[i] =
			dma_alloc_coherent(dev, vp9->segment_map_size,
					   &vp9->segment_map_dma[i], GFP_KERNEL);
		if (!vp9->segment_map[i])
			goto error;
	}

	memset(vp9->prob_count, 0, CEDRUS_DEC_VP9_PROB_COUNT_SIZE);
	memset(vp9->entry_info, 0, CEDRUS_DEC_VP9_ENTRY_INFO_SIZE);
	for (i = 0; i < ARRAY_SIZE(vp9->segment_map); i++)
		memset(vp9->segment_map[i], 0, vp9->segment_map_size);
	for (i = 0; i < ARRAY_SIZE(vp9->frame_context); i++)
		vp9->frame_context[i] = cedrus_vp9_default_probs;

	return 0;

error:
	cedrus_dec_vp9_cleanup(ctx);
	return ret;
}

static int cedrus_dec_vp9_job_prepare(struct cedrus_context *ctx)
{
	struct cedrus_dec_vp9_job *job = ctx->engine_job;

	job->frame = cedrus_context_ctrl_data(ctx,
					      V4L2_CID_STATELESS_VP9_FRAME);
	job->compressed_hdr = cedrus_context_ctrl_data(ctx,
					       V4L2_CID_STATELESS_VP9_COMPRESSED_HDR);

	if (!job->frame || !job->compressed_hdr)
		return -EINVAL;

	return 0;
}

static int cedrus_dec_vp9_format_configure(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	u32 value;
	int ret;

	/*
	 * The Android VE interface resets the complete block immediately before
	 * every VP9 run.  This clears the private entropy/probability state that
	 * is not covered by the documented VP9 register file or SRAM port.
	 */
	ret = reset_control_reset(dev->reset);
	if (ret)
		return ret;

	/* Match VeReset(): pulse the decoder block's internal reset as well. */
	value = cedrus_read(dev, VE_RESET_REG);
	cedrus_write(dev, VE_RESET_REG, value | VE_RESET_DECODER_RESET);
	value = cedrus_read(dev, VE_RESET_REG);
	cedrus_write(dev, VE_RESET_REG, value & ~VE_RESET_DECODER_RESET);

	/*
	 * H618's top-level decoder mode differs from the older Cedrus layout:
	 * this is the value emitted by libawvp9HwAL for both 320x240 and
	 * 1920x1080 VP9 jobs.  In particular, using the legacy 2 MiB
	 * reconstruction-field bit (bit 20) leaves the VP9 block idle.
	 */
	cedrus_write(dev, VE_MODE_REG, 0xc0020004);
	return 0;
}

static int cedrus_dec_vp9_job_configure(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	struct cedrus_dec_vp9_context *vp9 = ctx->engine_ctx;
	struct cedrus_dec_vp9_job *job = ctx->engine_job;
	struct cedrus_dec_vp9_buffer *vp9_buffer =
		cedrus_job_engine_buffer(ctx);
	const struct v4l2_ctrl_vp9_frame *frame = job->frame;
	struct v4l2_pix_format *coded = &ctx->v4l2.format_coded.fmt.pix;
	struct v4l2_pix_format *picture = &ctx->v4l2.format_picture.fmt.pix;
	dma_addr_t coded_dma, luma_dma, chroma_dma;
	u32 coded_addr, luma_addr, chroma_addr;
	unsigned int coded_size, coded_buffer_size, header_size;
	unsigned int width = frame->frame_width_minus_1 + 1;
	unsigned int height = frame->frame_height_minus_1 + 1;
	unsigned int sb_cols = DIV_ROUND_UP(width, 64);
	unsigned int sb_rows = DIV_ROUND_UP(height, 64);
	unsigned int tile_cols = 1U << frame->tile_cols_log2;
	unsigned int tile;
	u32 value;
	int ret;

	/*
	 * Keep unsupported state outside the triggerable path.  This is a real
	 * native decode path, but references/probability adaptation and 10-bit
	 * output are separate bring-up steps.
	 */
	if (!(frame->flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME) ||
	    frame->profile != 0 || frame->bit_depth != 8 ||
	    (frame->seg.flags & V4L2_VP9_SEGMENTATION_FLAG_ENABLED) ||
	    frame->tile_cols_log2 > 6 || frame->tile_rows_log2 ||
	    picture->pixelformat != V4L2_PIX_FMT_NV12 ||
	    width > coded->width || height > coded->height ||
	    job->compressed_hdr->tx_mode > V4L2_VP9_TX_MODE_SELECT)
		return -EOPNOTSUPP;

	header_size = frame->uncompressed_header_size +
		      frame->compressed_header_size;
	cedrus_job_buffer_coded_dma(ctx, &coded_dma, &coded_size);
	coded_buffer_size = vb2_plane_size(
		&ctx->job.buffer_coded->vb2_buf, 0);
	cedrus_job_buffer_picture_dma(ctx, &luma_dma, &chroma_dma);
	if (header_size >= coded_size || coded_size > U32_MAX / 8 ||
	    coded_buffer_size < coded_size)
		return -ERANGE;

	coded_addr = cedrus_dma_addr(dev, coded_dma);
	luma_addr = cedrus_dma_addr(dev, luma_dma);
	chroma_addr = cedrus_dma_addr(dev, chroma_dma);
	if (!coded_addr || !luma_addr || !chroma_addr ||
	    ((coded_addr | luma_addr | chroma_addr) & 0xff))
		return -ERANGE;

	ret = cedrus_dec_vp9_buffer_prepare(ctx, vp9_buffer, width, height);
	if (ret)
		return ret;

	/* Reset, forward-update and pack the exact context supplied by userspace. */
	memset(vp9->prob_count, 0, CEDRUS_DEC_VP9_PROB_COUNT_SIZE);
	vp9->frame_context_idx =
		cedrus_vp9_reset_frame_ctx(frame, vp9->frame_context);
	vp9->probability_tables =
		vp9->frame_context[vp9->frame_context_idx];
	cedrus_vp9_fw_update_probs(&vp9->probability_tables,
				   job->compressed_hdr, frame);
	cedrus_vp9_pack_probs((u8 *)vp9->prob_count +
			      CEDRUS_DEC_VP9_PROBS_OFFSET,
			      &vp9->probability_tables);
	/*
	 * The first tile is described by TILE_START/TILE_END.  CedarX places one
	 * four-word geometry record for every remaining tile at the start of the
	 * probability/entry buffer.  VP9's power-of-two partitioning deliberately
	 * uses floor boundaries (e.g. 30 SBs / 4 => 0,7,15,22,30).
	 */
	for (tile = 1; tile < tile_cols; tile++) {
		u32 *entry = (u32 *)vp9->prob_count + (tile - 1) * 4;
		u32 start = (tile * sb_cols) >> frame->tile_cols_log2;
		u32 end = (((tile + 1) * sb_cols) >>
			   frame->tile_cols_log2) - 1;

		entry[0] = 0;
		entry[1] = 0;
		entry[2] = start;
		entry[3] = ((sb_rows - 1) << 16) | end;
	}
	memset(vp9->entry_info, 0, CEDRUS_DEC_VP9_ENTRY_INFO_SIZE);
	memset(vp9->segment_map[0], 0, vp9->segment_map_size);
	memset(vp9->segment_map[1], 0, vp9->segment_map_size);

	cedrus_write(dev, VE_DEC_VP9_STATUS, VE_DEC_VP9_STATUS_MASK);

	value = CEDRUS_VP9_HDR_KEY_BASE |
		(tile_cols > 1 ? BIT(0) : 0) |
		(job->compressed_hdr->tx_mode << 20);
	cedrus_write(dev, VE_DEC_VP9_HDR_SYNC, value);
	cedrus_write(dev, VE_DEC_VP9_PIC_SIZE, (height << 16) | width);
	cedrus_write(dev, VE_DEC_VP9_LAST_PIC_SIZE, 0);
	cedrus_write(dev, VE_DEC_VP9_GOLDEN_PIC_SIZE, 0);
	cedrus_write(dev, VE_DEC_VP9_ALTREF_PIC_SIZE, 0);
	cedrus_write(dev, VE_DEC_VP9_LAST_SCALE0, 0);
	cedrus_write(dev, VE_DEC_VP9_LAST_SCALE1, 0x10000000);
	cedrus_write(dev, VE_DEC_VP9_GOLDEN_SCALE0, 0);
	cedrus_write(dev, VE_DEC_VP9_GOLDEN_SCALE1, 0);
	cedrus_write(dev, VE_DEC_VP9_ALTREF_SCALE0, 0);
	cedrus_write(dev, VE_DEC_VP9_ALTREF_SCALE1, 0);

	cedrus_write(dev, VE_DEC_VP9_SEGMENT_FEATURE, 0);
	cedrus_write(dev, VE_DEC_VP9_NEIGHBOR_ADDR,
		     VE_DEC_VP9_DMA_ADDR_BASE(
			     cedrus_dma_addr(dev, vp9->entry_info_dma)));
	cedrus_write(dev, VE_DEC_VP9_ENTRY_POINT_OFFSET_ADDR,
		     VE_DEC_VP9_DMA_ADDR_BASE(
			     cedrus_dma_addr(dev, vp9->prob_count_dma)));
	cedrus_write(dev, VE_DEC_VP9_TILE_START, 0);
	cedrus_write(dev, VE_DEC_VP9_TILE_END,
		     ((sb_rows - 1) << 16) |
		     ((sb_cols >> frame->tile_cols_log2) - 1));
	cedrus_write(dev, VE_DEC_VP9_TQ_BYPASS_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_VLD_BYPASS_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_COL_MV_ADDR,
		     VE_DEC_VP9_DMA_ADDR_BASE(
			     cedrus_dma_addr(dev, vp9_buffer->mv_col_dma)));
	cedrus_write(dev, VE_DEC_VP9_ENTROPY_LOWER8, 0);
	cedrus_write(dev, VE_DEC_VP9_FIRST_OUTPUT_OFFSET_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_10BIT_CONFIGURE, 0);

	cedrus_write(dev, VE_DEC_VP9_CURRENT_LUMA_ADDR,
		     VE_DEC_VP9_DMA_ADDR_BASE(luma_addr));
	cedrus_write(dev, VE_DEC_VP9_CURRENT_CHROMA_ADDR,
		     VE_DEC_VP9_DMA_ADDR_BASE(chroma_addr));
	cedrus_write(dev, VE_DEC_VP9_LAST_LUMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_LAST_CHROMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_GOLDEN_LUMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_GOLDEN_CHROMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_ALTREF_LUMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_ALTREF_CHROMA_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_SEGMENT_ID_ADDR, 0);
	cedrus_write(dev, VE_DEC_VP9_STD_BIT_OFFSET, 0);

	cedrus_write(dev, VE_PRIMARY_OUT_FMT, VE_PRIMARY_OUT_FMT_NV12);

	cedrus_vp9_dequant_write(ctx);
	cedrus_vp9_loop_filter_write(ctx);

	/*
	 * CedarX programs the complete picture, output and SRAM state before the
	 * stream DMA.  BITS_ADDR is not a passive base register on H618: setting
	 * VALID_DATA arms the bit reader and latches decoder state.  Keep it last
	 * both within the stream-register group and within picture setup.
	 */
	cedrus_write(dev, VE_DEC_VP9_BITS_OFFSET, header_size * 8);
	cedrus_write(dev, VE_DEC_VP9_BITS_END_ADDR,
		     VE_DEC_VP9_BITS_END_ADDR_BASE(coded_addr +
						   coded_buffer_size - 1));
	cedrus_write(dev, VE_DEC_VP9_BITS_LEN,
		     (coded_size - header_size) * 8);
	/* Publish the coherent probability/entry images before arming the VLD. */
	dma_wmb();
	value = VE_DEC_VP9_BITS_ADDR_BASE(coded_addr) |
		VE_DEC_VP9_BITS_ADDR_FIRST_FRAME |
		VE_DEC_VP9_BITS_ADDR_LAST_FRAME |
		VE_DEC_VP9_BITS_ADDR_VALID_DATA;
	cedrus_write(dev, VE_DEC_VP9_BITS_ADDR, value);

	cedrus_write(dev, VE_DEC_VP9_FUNC_CTRL,
		     VE_DEC_VP9_FUNC_CTRL_IRQ_MASK);
	return 0;
}

static void cedrus_dec_vp9_job_trigger(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	unsigned int offset;

	if (debug_vp9_regs) {
		for (offset = 0; offset < 0x100; offset += 0x10)
			dev_info(dev->dev,
				 "VP9-G %03x: %08x %08x %08x %08x\n",
				 offset, cedrus_read(dev, offset),
				 cedrus_read(dev, offset + 4),
				 cedrus_read(dev, offset + 8),
				 cedrus_read(dev, offset + 12));
		for (offset = 0; offset < 0xe0; offset += 0x10)
			dev_info(dev->dev,
				 "VP9-R %03x: %08x %08x %08x %08x\n",
				 0x500 + offset,
				 cedrus_read(dev, 0x500 + offset),
				 cedrus_read(dev, 0x504 + offset),
				 cedrus_read(dev, 0x508 + offset),
				 cedrus_read(dev, 0x50c + offset));
	}

	/* CedarX primes the probability/entry-point image with command 7. */
	cedrus_write(dev, VE_DEC_VP9_TRIGGER, VE_DEC_VP9_TRIGGER_PROBS);
	cedrus_write(dev, VE_DEC_VP9_TRIGGER, VE_DEC_VP9_TRIGGER_FRAME);
}

static void cedrus_dec_vp9_job_finish(struct cedrus_context *ctx, int state)
{
	struct cedrus_dec_vp9_context *vp9 = ctx->engine_ctx;
	const struct v4l2_ctrl_vp9_frame *frame =
		((struct cedrus_dec_vp9_job *)ctx->engine_job)->frame;

	if (state != VB2_BUF_STATE_DONE ||
	    !(frame->flags & V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX))
		return;

	/* Parallel mode has no backward probability adaptation. */
	if (frame->flags & V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE)
		vp9->frame_context[vp9->frame_context_idx] =
			vp9->probability_tables;
}

static int cedrus_dec_vp9_irq_status(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	u32 status = cedrus_read(dev, VE_DEC_VP9_STATUS) &
		     VE_DEC_VP9_STATUS_MASK;

	if (status && debug_vp9_regs)
		dev_info(dev->dev,
			 "VP9 done: trig=%08x status=%08x r53c=%08x bits=%08x/%08x/%08x/%08x r5b0=%08x\n",
			 cedrus_read(dev, VE_DEC_VP9_TRIGGER),
			 cedrus_read(dev, VE_DEC_VP9_STATUS),
			 cedrus_read(dev, VE_ENGINE_DEC_VP9 + 0x3c),
			 cedrus_read(dev, VE_DEC_VP9_BITS_ADDR),
			 cedrus_read(dev, VE_DEC_VP9_BITS_OFFSET),
			 cedrus_read(dev, VE_DEC_VP9_BITS_LEN),
			 cedrus_read(dev, VE_DEC_VP9_BITS_END_ADDR),
			 cedrus_read(dev, VE_DEC_VP9_SEGMENT_ID_ADDR));

	if (!status)
		return CEDRUS_IRQ_NONE;
	if (!(status & VE_DEC_VP9_STATUS_SUCCESS) ||
	    (status & VE_DEC_VP9_STATUS_ERROR_MASK))
		return CEDRUS_IRQ_ERROR;

	return CEDRUS_IRQ_SUCCESS;
}

static void cedrus_dec_vp9_irq_clear(struct cedrus_context *ctx)
{
	cedrus_write(ctx->proc->dev, VE_DEC_VP9_STATUS,
		     VE_DEC_VP9_STATUS_MASK);
}

static void cedrus_dec_vp9_irq_disable(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	u32 value = cedrus_read(dev, VE_DEC_VP9_FUNC_CTRL);

	cedrus_write(dev, VE_DEC_VP9_FUNC_CTRL,
		     value & ~VE_DEC_VP9_FUNC_CTRL_IRQ_MASK);
}

static const struct cedrus_engine_ops cedrus_dec_vp9_ops = {
	.format_prepare		= cedrus_dec_format_coded_prepare,
	.format_configure	= cedrus_dec_vp9_format_configure,

	.setup			= cedrus_dec_vp9_setup,
	.cleanup		= cedrus_dec_vp9_cleanup,

	.buffer_cleanup		= cedrus_dec_vp9_buffer_cleanup,

	.job_prepare		= cedrus_dec_vp9_job_prepare,
	.job_configure		= cedrus_dec_vp9_job_configure,
	.job_trigger		= cedrus_dec_vp9_job_trigger,
	.job_finish		= cedrus_dec_vp9_job_finish,

	.irq_status		= cedrus_dec_vp9_irq_status,
	.irq_clear		= cedrus_dec_vp9_irq_clear,
	.irq_disable		= cedrus_dec_vp9_irq_disable,
};

static const struct v4l2_ctrl_config cedrus_dec_vp9_ctrl_configs[] = {
	{
		.id = V4L2_CID_STATELESS_VP9_FRAME,
	},
	{
		.id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
	},
};

static const struct v4l2_frmsize_stepwise cedrus_dec_vp9_frmsize = {
	.min_width	= 64,
	.max_width	= 4096,
	.step_width	= 1,

	.min_height	= 64,
	.max_height	= 2304,
	.step_height	= 1,
};

const struct cedrus_engine cedrus_dec_vp9 = {
	.codec			= CEDRUS_CODEC_VP9,
	.role			= CEDRUS_ROLE_DECODER,
	.capabilities		= CEDRUS_CAPABILITY_VP9_DEC,

	.ops			= &cedrus_dec_vp9_ops,

	.pixelformat		= V4L2_PIX_FMT_VP9_FRAME,
	.ctrl_configs		= cedrus_dec_vp9_ctrl_configs,
	.ctrl_configs_count	= ARRAY_SIZE(cedrus_dec_vp9_ctrl_configs),
	.frmsize		= &cedrus_dec_vp9_frmsize,

	.ctx_size		= sizeof(struct cedrus_dec_vp9_context),
	.job_size		= sizeof(struct cedrus_dec_vp9_job),
	.buffer_size		= sizeof(struct cedrus_dec_vp9_buffer),
};
