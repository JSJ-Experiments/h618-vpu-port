// SPDX-License-Identifier: GPL-2.0
/*
 * H618 VP9 decoder scaffolding.
 *
 * The Android H618 VP9 HAL establishes the working-buffer sizes represented
 * here and drives a VP9-specific register interface in VE register group 5.
 * This engine is intentionally not present in cedrus_dec_engines[]: its job
 * configuration returns -EOPNOTSUPP and can never trigger the hardware while
 * the remaining control-to-register mapping is being implemented.
 */

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/overflow.h>

#include "cedrus.h"
#include "cedrus_context.h"
#include "cedrus_dec.h"
#include "cedrus_dec_vp9.h"
#include "cedrus_engine.h"

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

	return 0;
}

static int cedrus_dec_vp9_job_configure(struct cedrus_context *ctx)
{
	/* Do not permit a hardware trigger until every DMA address and bounded
	 * probability-table write has been mapped and validated. */
	return -EOPNOTSUPP;
}

static const struct cedrus_engine_ops cedrus_dec_vp9_ops = {
	.format_prepare		= cedrus_dec_format_coded_prepare,
	.format_configure	= cedrus_dec_format_coded_configure,

	.setup			= cedrus_dec_vp9_setup,
	.cleanup		= cedrus_dec_vp9_cleanup,

	.job_prepare		= cedrus_dec_vp9_job_prepare,
	.job_configure		= cedrus_dec_vp9_job_configure,
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
};
