/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 LoveSy <shana@zju.edu.cn> */

#ifndef _VS_OVERLAY_PLANE_REGS_H_
#define _VS_OVERLAY_PLANE_REGS_H_

#include <linux/bits.h>

#define VSDC_OVL_CONFIG(n)			(0x1540 + 0x4 * (n))
#define VSDC_OVL_CONFIG_ROT_MASK	GENMASK(4, 2)
#define VSDC_OVL_CONFIG_TILE_MODE_MASK	GENMASK(12, 8)
#define VSDC_OVL_CONFIG_SWIZZLE_MASK	GENMASK(14, 13)
#define VSDC_OVL_CONFIG_SWIZZLE(v)	((v) << 13)
#define VSDC_OVL_CONFIG_UV_SWIZZLE_EN	BIT(15)
#define VSDC_OVL_CONFIG_FMT_MASK	GENMASK(20, 16)
#define VSDC_OVL_CONFIG_FMT(v)		((v) << 16)
#define VSDC_OVL_CONFIG_EN		BIT(24)
#define VSDC_OVL_CONFIG_RGB_TO_RGB_EN	BIT(30)
#define VSDC_OVL_CONFIG_COMMIT		BIT(31)

#define VSDC_OVL_BLEND_CONFIG(n)	(0x1580 + 0x4 * (n))
#define VSDC_OVL_BLEND_CONFIG_PREMULTI	0x3450
#define VSDC_OVL_BLEND_CONFIG_COVERAGE	0x3950
#define VSDC_OVL_BLEND_CONFIG_PIXEL_NONE 0x3548

#define VSDC_OVL_ADDRESS(n)		(0x15c0 + 0x4 * (n))
#define VSDC_OVL_STRIDE(n)		(0x1600 + 0x4 * (n))

#define VSDC_OVL_TOP_LEFT(n)		(0x1640 + 0x4 * (n))
#define VSDC_OVL_BOTTOM_RIGHT(n)	(0x1680 + 0x4 * (n))

#define VSDC_OVL_SRC_GLOBAL_COLOR(n)	(0x16c0 + 0x4 * (n))
#define VSDC_OVL_DST_GLOBAL_COLOR(n)	(0x1700 + 0x4 * (n))

#define VSDC_OVL_SIZE(n)		(0x17c0 + 0x4 * (n))

#define VSDC_OVL_CONFIG_EX(n)		(0x2540 + 0x4 * (n))
#define VSDC_OVL_CONFIG_EX_ZPOS_MASK	GENMASK(2, 0)
#define VSDC_OVL_CONFIG_EX_ZPOS(v)	((v) << 0)
#define VSDC_OVL_CONFIG_EX_DISPLAY_ID_MASK BIT(3)
#define VSDC_OVL_CONFIG_EX_DISPLAY_ID(v)	((v) << 3)

#endif /* _VS_OVERLAY_PLANE_REGS_H_ */
