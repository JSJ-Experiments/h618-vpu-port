// SPDX-License-Identifier: GPL-2.0
/*
 * H618 JPEG encoder
 *
 * The H618 JPEG mode shares the AVC encoder and ISP input blocks.  Register
 * semantics and the quantization SRAM representation were established from
 * controlled single-frame traces of the Android 12 H618 VENC backend; JPEG
 * marker syntax and default tables are the standardized T.81 definitions.
 */

#include <linux/kernel.h>
#include <linux/videodev2.h>

#include "cedrus.h"
#include "cedrus_context.h"
#include "cedrus_enc.h"
#include "cedrus_enc_jpeg.h"
#include "cedrus_enc_jpeg_tables.h"
#include "cedrus_engine.h"
#include "cedrus_regs.h"

#define CEDRUS_ENC_JPEG_HEADER_SIZE	623

static int cedrus_enc_jpeg_ctrl_prepare(struct cedrus_context *ctx,
					struct v4l2_ctrl *ctrl)
{
	struct cedrus_enc_jpeg_context *jpeg = ctx->engine_ctx;

	/* Controls are created before the engine context exists. */
	if (!jpeg)
		return 0;

	if (ctrl->id == V4L2_CID_JPEG_COMPRESSION_QUALITY)
		jpeg->quality = ctrl->cur.val;

	return 0;
}

static int cedrus_enc_jpeg_setup(struct cedrus_context *ctx)
{
	return v4l2_ctrl_handler_setup(&ctx->v4l2.ctrl_handler);
}

static int cedrus_enc_jpeg_job_prepare(struct cedrus_context *ctx)
{
	struct cedrus_enc_jpeg_context *jpeg = ctx->engine_ctx;
	struct cedrus_enc_jpeg_job *job = ctx->engine_job;
	struct v4l2_ctrl_handler *handler = &ctx->v4l2.ctrl_handler;
	unsigned int scale;
	unsigned int quant;
	unsigned int i;

	mutex_lock(handler->lock);
	job->quality = clamp_t(unsigned int, jpeg->quality, 1, 100);
	mutex_unlock(handler->lock);

	if (job->quality < 50)
		scale = 5000 / job->quality;
	else
		scale = 200 - 2 * job->quality;

	for (i = 0; i < 64; i++) {
		quant = (cedrus_jpeg_luma_quant_base[i] * scale + 50) / 100;
		job->quant[0][i] = clamp_t(unsigned int, quant, 1, 255);
		quant = (cedrus_jpeg_chroma_quant_base[i] * scale + 50) / 100;
		job->quant[1][i] = clamp_t(unsigned int, quant, 1, 255);
	}

	return 0;
}

struct cedrus_enc_jpeg_header {
	u8 *data;
	size_t size;
	size_t position;
};

static int cedrus_enc_jpeg_put_byte(struct cedrus_enc_jpeg_header *header,
				    u8 byte)
{
	if (header->position >= header->size)
		return -ENOSPC;

	header->data[header->position++] = byte;

	return 0;
}

static int cedrus_enc_jpeg_put_array(struct cedrus_enc_jpeg_header *header,
				     const u8 *data, size_t size)
{
	unsigned int i;
	int ret;

	for (i = 0; i < size; i++) {
		ret = cedrus_enc_jpeg_put_byte(header, data[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int cedrus_enc_jpeg_put_u16(struct cedrus_enc_jpeg_header *header,
				   u16 value)
{
	int ret;

	ret = cedrus_enc_jpeg_put_byte(header, value >> 8);
	if (ret)
		return ret;

	return cedrus_enc_jpeg_put_byte(header, value);
}

static int cedrus_enc_jpeg_put_header(struct cedrus_context *ctx,
				      struct cedrus_enc_jpeg_header *header,
				      const u8 quant_tables[2][64])
{
	static const u8 soi_app0[] = {
		0xff, 0xd8,
		0xff, 0xe0, 0x00, 0x10,
		'J', 'F', 'I', 'F', 0x00,
		0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
	};
	static const u8 sos[] = {
		0xff, 0xda, 0x00, 0x0c, 0x03,
		0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
		0x00, 0x3f, 0x00,
	};
	static const u8 sof0[] = { 0xff, 0xc0, 0x00, 0x11, 0x08 };
	struct v4l2_pix_format *format =
		&ctx->v4l2.format_picture.fmt.pix;
	unsigned int table;
	unsigned int i;
	int ret;

	ret = cedrus_enc_jpeg_put_array(header, soi_app0, sizeof(soi_app0));
	if (ret)
		return ret;

	for (table = 0; table < 2; table++) {
		static const u8 dqt[] = { 0xff, 0xdb, 0x00, 0x43 };

		ret = cedrus_enc_jpeg_put_array(header, dqt, sizeof(dqt));
		if (ret)
			return ret;
		ret = cedrus_enc_jpeg_put_byte(header, table);
		if (ret)
			return ret;
		for (i = 0; i < 64; i++) {
			u8 quant =
				quant_tables[table][cedrus_jpeg_zigzag[i]];

			ret = cedrus_enc_jpeg_put_byte(header, quant);
			if (ret)
				return ret;
		}
	}

	ret = cedrus_enc_jpeg_put_array(header, sof0, sizeof(sof0));
	if (ret)
		return ret;
	ret = cedrus_enc_jpeg_put_u16(header, format->height);
	if (ret)
		return ret;
	ret = cedrus_enc_jpeg_put_u16(header, format->width);
	if (ret)
		return ret;
	ret = cedrus_enc_jpeg_put_array(header, (const u8[]){
		0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01,
		0x03, 0x11, 0x01,
	}, 10);
	if (ret)
		return ret;

	ret = cedrus_enc_jpeg_put_array(header, cedrus_jpeg_dht,
					ARRAY_SIZE(cedrus_jpeg_dht));
	if (ret)
		return ret;

	return cedrus_enc_jpeg_put_array(header, sos, sizeof(sos));
}

static u32 cedrus_enc_jpeg_quant_word(u8 quant)
{
	u32 reciprocal = 65536 / quant;

	if (reciprocal < U16_MAX)
		reciprocal++;
	else
		reciprocal = U16_MAX;

	return ((quant >> 1) << 16) | reciprocal;
}

static int cedrus_enc_jpeg_job_configure(struct cedrus_context *ctx)
{
	struct cedrus_enc_jpeg_job *job = ctx->engine_job;
	struct cedrus_device *dev = ctx->proc->dev;
	struct v4l2_pix_format *format =
		&ctx->v4l2.format_picture.fmt.pix;
	dma_addr_t addr;
	unsigned int size;
	unsigned int table;
	unsigned int i;
	u32 value;

	if (!dev->enc_addr_shift)
		return -EOPNOTSUPP;

	/* Select JPEG before programming the shared ISP/bitstream state. */
	cedrus_write(dev, VE_ENC_AVC_STARTTRIG_REG,
		     VE_ENC_AVC_STARTTRIG_ENCODE_MODE_JPEG);
	cedrus_write(dev, VE_ENC_AVC_PIC_INFO_REG,
		     (DIV_ROUND_UP(format->width, 8) << 16) |
		     DIV_ROUND_UP(format->height, 8));

	cedrus_job_buffer_coded_dma(ctx, &addr, &size);
	if (size < CEDRUS_ENC_JPEG_HEADER_SIZE + 1024 ||
	    size > U32_MAX / 8)
		return -EINVAL;
	job->header_size = CEDRUS_ENC_JPEG_HEADER_SIZE;

	cedrus_write(dev, VE_ENC_AVC_STM_START_ADDR_REG,
		     cedrus_enc_dma_addr(dev, addr));
	cedrus_write(dev, VE_ENC_AVC_STM_END_ADDR_REG,
		     cedrus_enc_dma_addr(dev, addr + size - 1));
	cedrus_write(dev, VE_ENC_AVC_STM_BIT_OFFSET_REG, 0);
	cedrus_write(dev, VE_ENC_AVC_STM_BIT_MAX_REG, size * 8);
	cedrus_write(dev, VE_ENC_AVC_STM_BIT_LEN_REG, 0);
	cedrus_write(dev, VE_ENC_AVC_HEADER_BITS_REG, 0);
	cedrus_write(dev, VE_ENC_AVC_RESIDUAL_BITS_REG, 0);

	/* Clear stale status before enabling the JPEG completion interrupt. */
	cedrus_write(dev, VE_ENC_AVC_PARA1_REG, 0xc0000000);
	cedrus_write(dev, VE_ENC_AVC_INT_EN_REG, 0xf);
	cedrus_write(dev, VE_ENC_AVC_STATUS_REG, 0x7);

	/* Two reciprocal DC scales are mirrored in PARA0. */
	value = 0xc0000000 |
		((1024 / job->quant[1][0]) << 16) |
		(1024 / job->quant[0][0]);
	cedrus_write(dev, VE_ENC_AVC_PARA0_REG, value);

	/* The SRAM port auto-increments after each data write. */
	cedrus_write(dev, VE_ENC_AVC_SRAM_OFFSET_REG, 0);
	for (table = 0; table < 2; table++)
		for (i = 0; i < 64; i++)
			cedrus_write(dev, VE_ENC_AVC_SRAM_DATA_REG,
				     cedrus_enc_jpeg_quant_word(job->quant[table][i]));

	return 0;
}

static void cedrus_enc_jpeg_job_trigger(struct cedrus_context *ctx)
{
	struct cedrus_device *dev = ctx->proc->dev;
	struct v4l2_pix_format *format =
		&ctx->v4l2.format_picture.fmt.pix;
	u32 value;

	value = BIT(25) | (DIV_ROUND_UP(format->height, 16) << 16) |
		VE_ENC_AVC_INT_EN_VALUE;
	cedrus_write(dev, VE_ENC_AVC_INT_EN_REG, value);
	cedrus_write(dev, VE_ENC_AVC_STARTTRIG_REG,
		     VE_ENC_AVC_STARTTRIG_ENCODE_MODE_JPEG |
		     VE_ENC_AVC_STARTTRIG_TYPE_ENC_START);
}

static void cedrus_enc_jpeg_job_finish(struct cedrus_context *ctx, int state)
{
	struct cedrus_enc_jpeg_job *job = ctx->engine_job;
	struct cedrus_buffer *buffer = cedrus_job_buffer_coded(ctx);
	struct cedrus_enc_jpeg_buffer *jpeg_buffer = buffer->engine_buffer;
	struct vb2_v4l2_buffer *v4l2_buffer = ctx->job.buffer_coded;
	struct vb2_buffer *vb2_buffer = &v4l2_buffer->vb2_buf;
	struct cedrus_device *dev = ctx->proc->dev;
	u32 length;

	if (state != VB2_BUF_STATE_DONE || !jpeg_buffer) {
		vb2_set_plane_payload(vb2_buffer, 0, 0);
		return;
	}
	memcpy(jpeg_buffer->quant, job->quant, sizeof(jpeg_buffer->quant));
	jpeg_buffer->header_size = job->header_size;

	length = cedrus_read(dev, VE_ENC_AVC_STM_BIT_LEN_REG);
	if ((length & 7) ||
	    length / 8 > vb2_plane_size(vb2_buffer, 0) -
			     job->header_size - 2) {
		vb2_set_plane_payload(vb2_buffer, 0, 0);
		v4l2_buffer->flags |= V4L2_BUF_FLAG_ERROR;
		return;
	}

	vb2_set_plane_payload(vb2_buffer, 0, length / 8);
	v4l2_buffer->flags |= V4L2_BUF_FLAG_KEYFRAME;
}

static void cedrus_enc_jpeg_buffer_finish(struct cedrus_context *ctx,
					  struct cedrus_buffer *buffer)
{
	struct vb2_v4l2_buffer *v4l2_buffer = &buffer->m2m_buffer.vb;
	struct vb2_buffer *vb2_buffer = &v4l2_buffer->vb2_buf;
	struct cedrus_enc_jpeg_buffer *jpeg_buffer = buffer->engine_buffer;
	struct cedrus_enc_jpeg_header header;
	unsigned int length = vb2_get_plane_payload(vb2_buffer, 0);
	u8 *coded;
	int ret;

	if (!jpeg_buffer ||
	    jpeg_buffer->header_size != CEDRUS_ENC_JPEG_HEADER_SIZE ||
	    vb2_buffer->state != VB2_BUF_STATE_DONE ||
	    v4l2_buffer->flags & V4L2_BUF_FLAG_ERROR)
		return;

	/*
	 * The H618 entropy engine pads to a byte boundary but emits neither the
	 * JPEG header nor EOI when started directly.  Its put-bits path is very
	 * sensitive to sequencing, so keep hardware limited to entropy coding.
	 * vb2 invokes ->buf_finish after synchronizing the capture buffer for
	 * CPU access; make room for the standards-defined header and add both
	 * markers here, outside hard-IRQ context.
	 */
	coded = vb2_plane_vaddr(vb2_buffer, 0);
	if (!coded || length > vb2_plane_size(vb2_buffer, 0) -
				   jpeg_buffer->header_size - 2) {
		vb2_set_plane_payload(vb2_buffer, 0, 0);
		v4l2_buffer->flags |= V4L2_BUF_FLAG_ERROR;
		return;
	}

	memmove(coded + jpeg_buffer->header_size, coded, length);
	header.data = coded;
	header.size = jpeg_buffer->header_size;
	header.position = 0;
	ret = cedrus_enc_jpeg_put_header(ctx, &header, jpeg_buffer->quant);
	if (ret || header.position != jpeg_buffer->header_size) {
		vb2_set_plane_payload(vb2_buffer, 0, 0);
		v4l2_buffer->flags |= V4L2_BUF_FLAG_ERROR;
		return;
	}

	length += jpeg_buffer->header_size;
	coded[length] = 0xff;
	coded[length + 1] = 0xd9;
	vb2_set_plane_payload(vb2_buffer, 0, length + 2);
}

static int cedrus_enc_jpeg_irq_status(struct cedrus_context *ctx)
{
	u32 status = cedrus_read(ctx->proc->dev, VE_ENC_AVC_STATUS_REG);

	if (!(status & VE_ENC_AVC_STATUS_MASK))
		return CEDRUS_IRQ_NONE;
	if (status & VE_ENC_AVC_STATUS_FINISH)
		return CEDRUS_IRQ_SUCCESS;

	return CEDRUS_IRQ_ERROR;
}

static void cedrus_enc_jpeg_irq_clear(struct cedrus_context *ctx)
{
	cedrus_write(ctx->proc->dev, VE_ENC_AVC_STATUS_REG,
		     VE_ENC_AVC_STATUS_MASK);
}

static void cedrus_enc_jpeg_irq_disable(struct cedrus_context *ctx)
{
	cedrus_write(ctx->proc->dev, VE_ENC_AVC_INT_EN_REG, 0);
}

static const struct cedrus_engine_ops cedrus_enc_jpeg_ops = {
	.ctrl_prepare		= cedrus_enc_jpeg_ctrl_prepare,
	.format_prepare		= cedrus_enc_format_coded_prepare,
	.format_configure	= cedrus_enc_format_coded_configure,
	.setup			= cedrus_enc_jpeg_setup,
	.buffer_finish		= cedrus_enc_jpeg_buffer_finish,
	.job_prepare		= cedrus_enc_jpeg_job_prepare,
	.job_configure		= cedrus_enc_jpeg_job_configure,
	.job_trigger		= cedrus_enc_jpeg_job_trigger,
	.job_finish		= cedrus_enc_jpeg_job_finish,
	.irq_status		= cedrus_enc_jpeg_irq_status,
	.irq_clear		= cedrus_enc_jpeg_irq_clear,
	.irq_disable		= cedrus_enc_jpeg_irq_disable,
};

static const struct v4l2_ctrl_config cedrus_enc_jpeg_ctrl_configs[] = {
	{
		.id	= V4L2_CID_JPEG_CHROMA_SUBSAMPLING,
		.min	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.max	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.def	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
	},
	{
		.id	= V4L2_CID_JPEG_COMPRESSION_QUALITY,
		.min	= 1,
		.max	= 100,
		.step	= 1,
		.def	= 90,
		.ops	= &cedrus_context_ctrl_ops,
	},
};

static const struct v4l2_frmsize_stepwise cedrus_enc_jpeg_frmsize = {
	.min_width	= 16,
	.max_width	= 1920,
	.step_width	= 2,
	.min_height	= 16,
	.max_height	= 1080,
	.step_height	= 2,
};

const struct cedrus_engine cedrus_enc_jpeg = {
	.codec			= CEDRUS_CODEC_JPEG,
	.role			= CEDRUS_ROLE_ENCODER,
	.capabilities		= CEDRUS_CAPABILITY_JPEG_ENC,
	.ops			= &cedrus_enc_jpeg_ops,
	.pixelformat		= V4L2_PIX_FMT_JPEG,
	.ctrl_configs		= cedrus_enc_jpeg_ctrl_configs,
	.ctrl_configs_count	= ARRAY_SIZE(cedrus_enc_jpeg_ctrl_configs),
	.frmsize		= &cedrus_enc_jpeg_frmsize,
	.ctx_size		= sizeof(struct cedrus_enc_jpeg_context),
	.job_size		= sizeof(struct cedrus_enc_jpeg_job),
	.coded_buffer_size	= sizeof(struct cedrus_enc_jpeg_buffer),
};
