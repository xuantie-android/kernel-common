// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VC8000E support for the T-Head TH1520.
 *
 * Copyright (C) 2026 LoveSy <shana@zju.edu.cn>
 */

#include "hantro.h"
#include "hantro_hw.h"

static const struct hantro_fmt th1520_vc8000e_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420P,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			/* Dummy reads are needed before this can be relaxed. */
			.step_height = 64,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420P,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			/* Dummy reads are needed before this can be relaxed. */
			.step_height = 64,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420SP,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			.step_height = MB_DIM,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420SP,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			.step_height = MB_DIM,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUYV422,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			.step_height = MB_DIM,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_UYVY422,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			.step_height = MB_DIM,
		},
	}, {
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.codec_mode = HANTRO_MODE_H264_ENC,
		.max_depth = 2,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_FHD_WIDTH,
			.step_width = MB_DIM,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_FHD_HEIGHT,
			.step_height = MB_DIM,
		},
	},
};

static const struct hantro_codec_ops th1520_vc8000e_codec_ops[] = {
	[HANTRO_MODE_H264_ENC] = {
		.run = hantro_vc8000e_h264_enc_run,
		.done = hantro_vc8000e_h264_enc_done,
		.init = hantro_vc8000e_h264_enc_init,
		.exit = hantro_vc8000e_h264_enc_exit,
	},
};

static const struct hantro_irq th1520_vc8000e_irqs[] = {
	{ "vc8000e", hantro_vc8000e_irq },
};

static const char * const th1520_vc8000e_clk_names[] = {
	"aclk", "cclk", "pclk",
};

const struct hantro_variant th1520_vc8000e_variant = {
	.enc_offset = 0x1000,
	.enc_fmts = th1520_vc8000e_fmts,
	.num_enc_fmts = ARRAY_SIZE(th1520_vc8000e_fmts),
	.codec = HANTRO_H264_ENCODER,
	.codec_ops = th1520_vc8000e_codec_ops,
	.irqs = th1520_vc8000e_irqs,
	.num_irqs = ARRAY_SIZE(th1520_vc8000e_irqs),
	.clk_names = th1520_vc8000e_clk_names,
	.num_clocks = ARRAY_SIZE(th1520_vc8000e_clk_names),
};
