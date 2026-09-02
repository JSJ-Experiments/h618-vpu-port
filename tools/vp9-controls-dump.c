// SPDX-License-Identifier: MIT
/* Dump Linux stateless VP9 controls from an IVF stream.
 *
 * This intentionally uses GStreamer's stateful VP9 parser: unlike the older
 * public GstVp9Parser, it also parses the compressed probability-update
 * header required by V4L2_CID_STATELESS_VP9_COMPRESSED_HDR.
 */
#define GST_USE_UNSTABLE_API
#include <gst/codecs/gstvp9statefulparser.h>
#include <linux/v4l2-controls.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ivf_header {
	uint8_t signature[4];
	uint16_t version;
	uint16_t header_size;
	uint8_t fourcc[4];
	uint16_t width;
	uint16_t height;
	uint32_t rate;
	uint32_t scale;
	uint32_t frame_count;
	uint32_t unused;
} __attribute__((packed));

struct ivf_frame_header {
	uint32_t size;
	uint64_t timestamp;
} __attribute__((packed));

static uint16_t le16(uint16_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return v;
#else
	return __builtin_bswap16(v);
#endif
}

static uint32_t le32(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return v;
#else
	return __builtin_bswap32(v);
#endif
}

static int write_blob(const char *prefix, unsigned int index,
		      const char *suffix, const void *data, size_t size)
{
	char path[4096];
	FILE *file;

	if (snprintf(path, sizeof(path), "%s-%03u.%s", prefix, index, suffix) >=
	    (int)sizeof(path)) {
		fprintf(stderr, "output path is too long\n");
		return -1;
	}
	file = fopen(path, "wb");
	if (!file || fwrite(data, 1, size, file) != size) {
		fprintf(stderr, "write %s: %s\n", path, strerror(errno));
		if (file)
			fclose(file);
		return -1;
	}
	if (fclose(file)) {
		fprintf(stderr, "close %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}

static void fill_loop_filter(struct v4l2_vp9_loop_filter *dst,
			     const GstVp9LoopFilterParams *src)
{
	if (src->loop_filter_delta_enabled)
		dst->flags |= V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED;
	if (src->loop_filter_delta_update)
		dst->flags |= V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE;
	dst->level = src->loop_filter_level;
	dst->sharpness = src->loop_filter_sharpness;
	memcpy(dst->ref_deltas, src->loop_filter_ref_deltas,
	       sizeof(dst->ref_deltas));
	memcpy(dst->mode_deltas, src->loop_filter_mode_deltas,
	       sizeof(dst->mode_deltas));
}

static void fill_segmentation(struct v4l2_vp9_segmentation *dst,
			      const GstVp9SegmentationParams *src)
{
	unsigned int segment, feature;

	if (src->segmentation_enabled)
		dst->flags |= V4L2_VP9_SEGMENTATION_FLAG_ENABLED;
	if (src->segmentation_update_map)
		dst->flags |= V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP;
	if (src->segmentation_temporal_update)
		dst->flags |= V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE;
	if (src->segmentation_update_data)
		dst->flags |= V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA;
	if (src->segmentation_abs_or_delta_update)
		dst->flags |= V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE;
	memcpy(dst->tree_probs, src->segmentation_tree_probs,
	       sizeof(dst->tree_probs));
	memcpy(dst->pred_probs, src->segmentation_pred_prob,
	       sizeof(dst->pred_probs));
	for (segment = 0; segment < 8; segment++) {
		for (feature = 0; feature < 4; feature++) {
			if (src->feature_enabled[segment][feature])
				dst->feature_enabled[segment] |=
					V4L2_VP9_SEGMENT_FEATURE_ENABLED(feature);
			dst->feature_data[segment][feature] =
				src->feature_data[segment][feature];
		}
	}
}

static void fill_probs(struct v4l2_ctrl_vp9_compressed_hdr *dst,
		       const GstVp9FrameHeader *src)
{
	const GstVp9DeltaProbabilities *p = &src->delta_probabilities;

	memset(dst, 0, sizeof(*dst));
	dst->tx_mode = src->tx_mode;
	memcpy(dst->tx8, p->tx_probs_8x8, sizeof(dst->tx8));
	memcpy(dst->tx16, p->tx_probs_16x16, sizeof(dst->tx16));
	memcpy(dst->tx32, p->tx_probs_32x32, sizeof(dst->tx32));
	memcpy(dst->coef, p->coef, sizeof(dst->coef));
	memcpy(dst->skip, p->skip, sizeof(dst->skip));
	memcpy(dst->inter_mode, p->inter_mode, sizeof(dst->inter_mode));
	memcpy(dst->interp_filter, p->interp_filter, sizeof(dst->interp_filter));
	memcpy(dst->is_inter, p->is_inter, sizeof(dst->is_inter));
	memcpy(dst->comp_mode, p->comp_mode, sizeof(dst->comp_mode));
	memcpy(dst->single_ref, p->single_ref, sizeof(dst->single_ref));
	memcpy(dst->comp_ref, p->comp_ref, sizeof(dst->comp_ref));
	memcpy(dst->y_mode, p->y_mode, sizeof(dst->y_mode));
	memcpy(dst->partition, p->partition, sizeof(dst->partition));
	memcpy(dst->mv.joint, p->mv.joint, sizeof(dst->mv.joint));
	memcpy(dst->mv.sign, p->mv.sign, sizeof(dst->mv.sign));
	memcpy(dst->mv.classes, p->mv.klass, sizeof(dst->mv.classes));
	memcpy(dst->mv.class0_bit, p->mv.class0_bit,
	       sizeof(dst->mv.class0_bit));
	memcpy(dst->mv.bits, p->mv.bits, sizeof(dst->mv.bits));
	memcpy(dst->mv.class0_fr, p->mv.class0_fr,
	       sizeof(dst->mv.class0_fr));
	memcpy(dst->mv.fr, p->mv.fr, sizeof(dst->mv.fr));
	memcpy(dst->mv.class0_hp, p->mv.class0_hp,
	       sizeof(dst->mv.class0_hp));
	memcpy(dst->mv.hp, p->mv.hp, sizeof(dst->mv.hp));
}

static void fill_frame(struct v4l2_ctrl_vp9_frame *dst,
		       const GstVp9FrameHeader *src, const uint64_t refs[8])
{
	memset(dst, 0, sizeof(*dst));
	if (src->frame_type == GST_VP9_KEY_FRAME)
		dst->flags |= V4L2_VP9_FRAME_FLAG_KEY_FRAME;
	if (src->show_frame)
		dst->flags |= V4L2_VP9_FRAME_FLAG_SHOW_FRAME;
	if (src->error_resilient_mode)
		dst->flags |= V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT;
	if (src->intra_only)
		dst->flags |= V4L2_VP9_FRAME_FLAG_INTRA_ONLY;
	if (src->allow_high_precision_mv)
		dst->flags |= V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV;
	if (src->refresh_frame_context)
		dst->flags |= V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX;
	if (src->frame_parallel_decoding_mode)
		dst->flags |= V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE;
	if (src->subsampling_x)
		dst->flags |= V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING;
	if (src->subsampling_y)
		dst->flags |= V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING;
	if (src->color_range)
		dst->flags |= V4L2_VP9_FRAME_FLAG_COLOR_RANGE_FULL_SWING;

	dst->compressed_header_size = src->header_size_in_bytes;
	dst->uncompressed_header_size = src->frame_header_length_in_bytes;
	dst->profile = src->profile;
	dst->reset_frame_context = src->reset_frame_context < 2 ?
		V4L2_VP9_RESET_FRAME_CTX_NONE :
		(src->reset_frame_context == 2 ? V4L2_VP9_RESET_FRAME_CTX_SPEC :
		 V4L2_VP9_RESET_FRAME_CTX_ALL);
	dst->frame_context_idx = src->frame_context_idx;
	dst->bit_depth = src->bit_depth;
	dst->interpolation_filter = src->interpolation_filter;
	dst->tile_cols_log2 = src->tile_cols_log2;
	dst->tile_rows_log2 = src->tile_rows_log2;
	dst->reference_mode = src->reference_mode;
	dst->frame_width_minus_1 = src->width - 1;
	dst->frame_height_minus_1 = src->height - 1;
	dst->render_width_minus_1 =
		(src->render_width ? src->render_width : src->width) - 1;
	dst->render_height_minus_1 =
		(src->render_height ? src->render_height : src->height) - 1;
	if (src->ref_frame_sign_bias[GST_VP9_REF_FRAME_LAST])
		dst->ref_frame_sign_bias |= V4L2_VP9_SIGN_BIAS_LAST;
	if (src->ref_frame_sign_bias[GST_VP9_REF_FRAME_GOLDEN])
		dst->ref_frame_sign_bias |= V4L2_VP9_SIGN_BIAS_GOLDEN;
	if (src->ref_frame_sign_bias[GST_VP9_REF_FRAME_ALTREF])
		dst->ref_frame_sign_bias |= V4L2_VP9_SIGN_BIAS_ALT;
	if (src->frame_type != GST_VP9_KEY_FRAME && !src->intra_only) {
		dst->last_frame_ts = refs[src->ref_frame_idx[0]];
		dst->golden_frame_ts = refs[src->ref_frame_idx[1]];
		dst->alt_frame_ts = refs[src->ref_frame_idx[2]];
	}
	fill_loop_filter(&dst->lf, &src->loop_filter_params);
	dst->quant.base_q_idx = src->quantization_params.base_q_idx;
	dst->quant.delta_q_y_dc = src->quantization_params.delta_q_y_dc;
	dst->quant.delta_q_uv_dc = src->quantization_params.delta_q_uv_dc;
	dst->quant.delta_q_uv_ac = src->quantization_params.delta_q_uv_ac;
	fill_segmentation(&dst->seg, &src->segmentation_params);
}

int main(int argc, char **argv)
{
	struct ivf_header ivf;
	struct ivf_frame_header fh;
	GstVp9StatefulParser *parser;
	uint64_t refs[8] = { 0 };
	const char *prefix;
	FILE *file;
	unsigned int index = 0;
	int ret = 1;

	if (argc != 3) {
		fprintf(stderr, "usage: %s INPUT.ivf OUTPUT_PREFIX\n", argv[0]);
		return 64;
	}
	prefix = argv[2];
	file = fopen(argv[1], "rb");
	if (!file) {
		fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno));
		return 66;
	}
	if (fread(&ivf, 1, sizeof(ivf), file) != sizeof(ivf) ||
	    memcmp(ivf.signature, "DKIF", 4) || memcmp(ivf.fourcc, "VP90", 4) ||
	    le16(ivf.header_size) != sizeof(ivf)) {
		fprintf(stderr, "not a supported VP9 IVF file\n");
		goto out_file;
	}
	parser = gst_vp9_stateful_parser_new();
	if (!parser) {
		fprintf(stderr, "could not allocate VP9 parser\n");
		goto out_file;
	}

	while (fread(&fh, 1, sizeof(fh), file) == sizeof(fh)) {
		GstVp9FrameHeader h;
		struct v4l2_ctrl_vp9_compressed_hdr probs;
		struct v4l2_ctrl_vp9_frame frame;
		uint32_t size = le32(fh.size);
		uint64_t timestamp = ((uint64_t)index + 1) * 1000000000ULL;
		uint8_t *data = malloc(size);
		unsigned int slot;

		if (!size || !data || fread(data, 1, size, file) != size) {
			fprintf(stderr, "read IVF frame %u: %s\n", index,
				errno ? strerror(errno) : "truncated input");
			free(data);
			goto out_parser;
		}
		if (gst_vp9_stateful_parser_parse_uncompressed_frame_header(
			    parser, &h, data, size) != GST_VP9_PARSER_OK ||
		    h.show_existing_frame ||
		    gst_vp9_stateful_parser_parse_compressed_frame_header(
			    parser, &h, data + h.frame_header_length_in_bytes,
			    size - h.frame_header_length_in_bytes) !=
			    GST_VP9_PARSER_OK) {
			fprintf(stderr, "parse VP9 frame %u failed\n", index);
			free(data);
			goto out_parser;
		}
		fill_frame(&frame, &h, refs);
		fill_probs(&probs, &h);
		printf("frame=%u bytes=%u type=%s size=%ux%u uhdr=%u chdr=%u "
		       "q=%u lf=%u tx=%u refs=%" PRIu64 ",%" PRIu64 ",%" PRIu64
		       " refresh=%02x flags=%08x ctx=%u reset=%u interp=%u refmode=%u "
		       "refidx=%u,%u,%u sign=%x\n",
		       index, size, h.frame_type == GST_VP9_KEY_FRAME ? "key" : "inter",
		       h.width, h.height, h.frame_header_length_in_bytes,
		       h.header_size_in_bytes, h.quantization_params.base_q_idx,
		       h.loop_filter_params.loop_filter_level, h.tx_mode,
		       (uint64_t)frame.last_frame_ts,
		       (uint64_t)frame.golden_frame_ts,
		       (uint64_t)frame.alt_frame_ts,
		       h.refresh_frame_flags, frame.flags, h.frame_context_idx,
		       h.reset_frame_context, h.interpolation_filter,
		       h.reference_mode, h.ref_frame_idx[0], h.ref_frame_idx[1],
		       h.ref_frame_idx[2], frame.ref_frame_sign_bias);
		if (write_blob(prefix, index, "vp9", data, size) ||
		    write_blob(prefix, index, "frame", &frame, sizeof(frame)) ||
		    write_blob(prefix, index, "probs", &probs, sizeof(probs))) {
			free(data);
			goto out_parser;
		}
		for (slot = 0; slot < 8; slot++) {
			if (h.refresh_frame_flags & (1U << slot))
				refs[slot] = timestamp;
		}
		free(data);
		index++;
	}
	if (ferror(file)) {
		fprintf(stderr, "read %s: %s\n", argv[1], strerror(errno));
		goto out_parser;
	}
	ret = 0;
out_parser:
	gst_vp9_stateful_parser_free(parser);
out_file:
	fclose(file);
	return ret;
}
