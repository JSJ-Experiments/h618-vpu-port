/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CEDRUS_ENC_JPEG_H_
#define _CEDRUS_ENC_JPEG_H_

struct cedrus_enc_jpeg_context {
	int quality;
};

struct cedrus_enc_jpeg_job {
	unsigned int quality;
	u8 quant[2][64];
	unsigned int header_size;
};

struct cedrus_enc_jpeg_buffer {
	u8 quant[2][64];
	unsigned int header_size;
};

extern const struct cedrus_engine cedrus_enc_jpeg;

#endif
