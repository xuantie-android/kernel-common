// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 LoveSy <shana@zju.edu.cn>
 */

#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_overlay_plane_regs.h"
#include "vs_plane.h"

struct vs_overlay_plane {
	struct drm_plane base;
	struct vs_dc *dc;
	unsigned int id;
};

static inline struct vs_overlay_plane *
to_vs_overlay_plane(struct drm_plane *plane)
{
	return container_of(plane, struct vs_overlay_plane, base);
}

static int vs_overlay_plane_atomic_check(struct drm_plane *plane,
					 struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct vs_plane_state *new_vs_state = to_vs_plane_state(new_state);
	struct drm_crtc_state *crtc_state = NULL;
	int ret;

	if (new_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
						      new_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
					  DRM_PLANE_NO_SCALING,
					  DRM_PLANE_NO_SCALING,
					  true, true);
	if (ret || !new_state->visible)
		return ret;

	ret = drm_format_to_vs_format(new_state->fb->format->format,
					      &new_vs_state->format);
	if (drm_WARN_ON_ONCE(plane->dev, ret))
		return ret;

	return 0;
}

static void vs_overlay_plane_commit(struct vs_overlay_plane *overlay)
{
	regmap_set_bits(overlay->dc->regs, VSDC_OVL_CONFIG(overlay->id),
			VSDC_OVL_CONFIG_COMMIT);
}

static void vs_overlay_plane_atomic_enable(struct drm_plane *plane,
					   struct drm_atomic_commit *state)
{
	struct vs_overlay_plane *overlay = to_vs_overlay_plane(plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(new_state->crtc);

	regmap_update_bits(overlay->dc->regs, VSDC_OVL_CONFIG_EX(overlay->id),
			   VSDC_OVL_CONFIG_EX_DISPLAY_ID_MASK,
			   VSDC_OVL_CONFIG_EX_DISPLAY_ID(vcrtc->id));
	regmap_set_bits(overlay->dc->regs, VSDC_OVL_CONFIG(overlay->id),
			VSDC_OVL_CONFIG_EN);

	vs_overlay_plane_commit(overlay);
}

static void vs_overlay_plane_atomic_disable(struct drm_plane *plane,
					    struct drm_atomic_commit *state)
{
	struct vs_overlay_plane *overlay = to_vs_overlay_plane(plane);

	regmap_clear_bits(overlay->dc->regs, VSDC_OVL_CONFIG(overlay->id),
			  VSDC_OVL_CONFIG_EN);
	vs_overlay_plane_commit(overlay);
}

static u32 vs_overlay_blend_config(const struct drm_plane_state *state)
{
	switch (state->pixel_blend_mode) {
	case DRM_MODE_BLEND_PREMULTI:
		return VSDC_OVL_BLEND_CONFIG_PREMULTI;
	case DRM_MODE_BLEND_COVERAGE:
		return VSDC_OVL_BLEND_CONFIG_COVERAGE;
	case DRM_MODE_BLEND_PIXEL_NONE:
	default:
		return VSDC_OVL_BLEND_CONFIG_PIXEL_NONE;
	}
}

static void vs_overlay_plane_atomic_update(struct drm_plane *plane,
					   struct drm_atomic_commit *atomic_state)
{
	struct vs_overlay_plane *overlay = to_vs_overlay_plane(plane);
	struct drm_plane_state *state =
		drm_atomic_get_new_plane_state(atomic_state, plane);
	struct vs_plane_state *vs_state = to_vs_plane_state(state);
	struct vs_crtc *vcrtc;
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t dma_addr;
	u32 alpha;

	if (!state->visible) {
		vs_overlay_plane_atomic_disable(plane, atomic_state);
		return;
	}

	vcrtc = drm_crtc_to_vs_crtc(state->crtc);
	dma_addr = vs_fb_get_dma_addr(fb, &state->src);
	alpha = (state->alpha >> 8) << 24;

	regmap_update_bits(overlay->dc->regs, VSDC_OVL_CONFIG(overlay->id),
			   VSDC_OVL_CONFIG_FMT_MASK |
			   VSDC_OVL_CONFIG_SWIZZLE_MASK |
			   VSDC_OVL_CONFIG_UV_SWIZZLE_EN |
			   VSDC_OVL_CONFIG_RGB_TO_RGB_EN |
			   VSDC_OVL_CONFIG_TILE_MODE_MASK |
			   VSDC_OVL_CONFIG_ROT_MASK,
			   VSDC_OVL_CONFIG_FMT(vs_state->format.color) |
			   VSDC_OVL_CONFIG_SWIZZLE(vs_state->format.swizzle) |
			   (vs_state->format.uv_swizzle ?
			    VSDC_OVL_CONFIG_UV_SWIZZLE_EN : 0));
	regmap_update_bits(overlay->dc->regs, VSDC_OVL_CONFIG_EX(overlay->id),
			   VSDC_OVL_CONFIG_EX_ZPOS_MASK |
			   VSDC_OVL_CONFIG_EX_DISPLAY_ID_MASK,
			   VSDC_OVL_CONFIG_EX_ZPOS(state->zpos) |
			   VSDC_OVL_CONFIG_EX_DISPLAY_ID(vcrtc->id));

	regmap_write(overlay->dc->regs, VSDC_OVL_ADDRESS(overlay->id),
		     lower_32_bits(dma_addr));
	regmap_write(overlay->dc->regs, VSDC_OVL_STRIDE(overlay->id),
		     fb->pitches[0]);
	regmap_write(overlay->dc->regs, VSDC_OVL_SIZE(overlay->id),
		     VSDC_MAKE_PLANE_SIZE(drm_rect_width(&state->src) >> 16,
					  drm_rect_height(&state->src) >> 16));
	regmap_write(overlay->dc->regs, VSDC_OVL_TOP_LEFT(overlay->id),
		     VSDC_MAKE_PLANE_POS(state->dst.x1, state->dst.y1));
	regmap_write(overlay->dc->regs, VSDC_OVL_BOTTOM_RIGHT(overlay->id),
		     VSDC_MAKE_PLANE_POS(state->dst.x2, state->dst.y2));

	regmap_write(overlay->dc->regs, VSDC_OVL_SRC_GLOBAL_COLOR(overlay->id),
		     alpha);
	regmap_write(overlay->dc->regs, VSDC_OVL_DST_GLOBAL_COLOR(overlay->id),
		     alpha);
	regmap_write(overlay->dc->regs, VSDC_OVL_BLEND_CONFIG(overlay->id),
		     vs_overlay_blend_config(state));

	vs_overlay_plane_commit(overlay);
}

static const struct drm_plane_helper_funcs vs_overlay_plane_helper_funcs = {
	.atomic_check	= vs_overlay_plane_atomic_check,
	.atomic_update	= vs_overlay_plane_atomic_update,
	.atomic_enable	= vs_overlay_plane_atomic_enable,
	.atomic_disable	= vs_overlay_plane_atomic_disable,
};

static const struct drm_plane_funcs vs_overlay_plane_funcs = {
	.atomic_destroy_state	= vs_plane_destroy_state,
	.atomic_duplicate_state	= vs_plane_duplicate_state,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= vs_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

struct drm_plane *vs_overlay_plane_init(struct drm_device *drm_dev,
					struct vs_dc *dc, unsigned int id,
					u32 possible_crtcs)
{
	struct vs_overlay_plane *overlay;
	unsigned int supported_blends = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					BIT(DRM_MODE_BLEND_PREMULTI) |
					BIT(DRM_MODE_BLEND_COVERAGE);
	unsigned int max_zpos = dc->identity.display_count +
				 dc->identity.overlay_count - 1;
	int ret;

	overlay = drmm_universal_plane_alloc(drm_dev, struct vs_overlay_plane,
					     base, possible_crtcs,
					     &vs_overlay_plane_funcs,
					     dc->identity.formats->array,
					     dc->identity.formats->num,
					     NULL, DRM_PLANE_TYPE_OVERLAY,
					     "overlay%u", id);
	if (IS_ERR(overlay))
		return ERR_CAST(overlay);

	overlay->dc = dc;
	overlay->id = id;
	drm_plane_helper_add(&overlay->base, &vs_overlay_plane_helper_funcs);

	ret = drm_plane_create_alpha_property(&overlay->base);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_plane_create_blend_mode_property(&overlay->base,
						    supported_blends);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_plane_create_zpos_property(&overlay->base, id + 1, 0,
					     max_zpos);
	if (ret)
		return ERR_PTR(ret);

	return &overlay->base;
}
