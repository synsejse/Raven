/**
 * Raven - widgets/common.c
 * Helpers shared by the widget implementations.
 */

#include "common.h"

/* ---------------------------------------------------------------------------
 *  Text
 * ------------------------------------------------------------------------- */

float rvn_widget_font_size(const rvn_widget_t* widget, float size) {
	return size > 0.0f ? size : rvn_widget_metric(widget, R_METRIC_FONT_SIZE);
}

float rvn_widget_text_width(const rvn_widget_t* widget, Rid font, float size, const char* text) {
	rvn_font_t* resolved = rvn_font_resolve(font);
	if (!resolved || !text) return 0.0f;
	return rvn_text_width(resolved, rvn_widget_font_size(widget, size), text, -1);
}

float rvn_widget_line_height(const rvn_widget_t* widget, Rid font, float size) {
	rvn_font_t* resolved = rvn_font_resolve(font);
	rvn_font_metrics_t metrics;
	if (!resolved) return 0.0f;
	rvn_font_metrics(resolved, rvn_widget_font_size(widget, size), &metrics);
	return metrics.line_height;
}

int rvn_widget_alive(const rvn_widget_t* widget) {
	return (widget->flags & RVN_WF_DEAD) == 0;
}

/* ---------------------------------------------------------------------------
 *  Glass themes
 * ------------------------------------------------------------------------- */

float rvn_control_radius(const rvn_widget_t* widget, float height) {
	/* Glass defaults to a capsule, unless the widget or its style gives a radius. */
	if (rvn_widget_glass(widget) && !rvn_widget_has_metric(widget, R_METRIC_CORNER_RADIUS)) return height * 0.5f;
	return rvn_widget_metric(widget, R_METRIC_CORNER_RADIUS);
}

/* Color between two roles, with the alpha interpolated too. */
static rvn_color_t rvn_role_mix(const rvn_widget_t* widget, RColorRole from, RColorRole to, float t) {
	return rvn_color_mix(rvn_widget_color(widget, from), rvn_widget_color(widget, to), rvn_clampf(t, 0.0f, 1.0f));
}

void rvn_glass_style(const rvn_widget_t* widget, rvn_surface_t kind, float hover, float press, rvn_glass_t* glass) {
	const int dark = rvn_widget_theme(widget)->dark;
	/* the rim catches more light on a pale surface */
	float rim = dark ? 0.55f : 0.85f;
	rvn_color_t tint;

	/* springs run past their ends: the look only follows them between 0 and 1 */
	hover = rvn_clampf(hover, 0.0f, 1.0f);
	press = rvn_clampf(press, 0.0f, 1.0f);
	memset(glass, 0, sizeof(*glass));
	glass->frost = 1.0f;
	glass->refraction = 10.0f;
	glass->saturation = 1.5f;
	glass->shadow_color = rvn_widget_color(widget, R_COLOR_SHADOW);

	switch (kind) {
		case RVN_SURFACE_ACCENT:
			tint = rvn_role_mix(widget, R_COLOR_ACCENT, R_COLOR_ACCENT_HOVERED, hover);
			tint = rvn_color_mix(tint, rvn_widget_color(widget, R_COLOR_ACCENT_PRESSED), press);
			/* tinted glass: the color dominates but what is behind still shows */
			tint = rvn_color_fade(tint, 0.84f);
			glass->saturation = 1.3f;
			glass->shadow = 7.0f;
			break;
		case RVN_SURFACE_INPUT:
			tint = rvn_widget_color(widget, R_COLOR_INPUT);
			rim *= 0.55f;
			glass->refraction = 6.0f;
			break;
		case RVN_SURFACE_BAR:
			tint = rvn_widget_color(widget, R_COLOR_PANEL);
			rim *= 0.6f;
			glass->refraction = 8.0f;
			glass->bevel = 6.0f;
			break;
		case RVN_SURFACE_PANEL:
			tint = rvn_widget_color(widget, R_COLOR_PANEL);
			glass->refraction = 14.0f;
			glass->bevel = 12.0f;
			break;
		case RVN_SURFACE_POPUP:
			tint = rvn_widget_color(widget, R_COLOR_POPUP);
			glass->refraction = 14.0f;
			glass->bevel = 10.0f;
			glass->saturation = 1.7f;
			glass->shadow = 22.0f;
			break;
		case RVN_SURFACE_LENS:
			tint = rvn_rgba(1.0f, 1.0f, 1.0f, dark ? 0.06f : 0.16f);
			rim = 1.0f;
			glass->frost = 0.0f;
			glass->refraction = 16.0f;
			glass->saturation = 1.15f;
			glass->shadow = 9.0f;
			break;
		case RVN_SURFACE_CONTROL:
		default:
			tint = rvn_role_mix(widget, R_COLOR_CONTROL, R_COLOR_CONTROL_HOVERED, hover);
			tint = rvn_color_mix(tint, rvn_widget_color(widget, R_COLOR_CONTROL_PRESSED), press);
			glass->frost = 0.9f;
			glass->shadow = 6.0f;
			break;
	}
	/* the material: how much of what is behind comes through, and in which color */
	{
		float alpha = (float)rvn_color_a(tint) / 255.0f;
		switch (rvn_widget_material(widget)) {
			case R_MATERIAL_GLASS:
				/* clear: barely tinted, barely blurred, and a rim that bends like a lens */
				tint = kind == RVN_SURFACE_ACCENT ? rvn_color_fade(tint, 0.82f) : rvn_color_fade(tint, 0.55f);
				glass->frost *= 0.22f;
				glass->refraction *= 1.5f;
				glass->saturation = 1.0f + (glass->saturation - 1.0f) * 0.5f;
				rim = rvn_minf(rim * 1.2f, 1.0f);
				break;
			case R_MATERIAL_TINTED_GLASS:
				if (kind != RVN_SURFACE_ACCENT && kind != RVN_SURFACE_LENS) {
					/* frosted, and colored: the accent shows through every surface, stronger on what floats higher */
					float strength = kind == RVN_SURFACE_INPUT ? 0.25f : (kind == RVN_SURFACE_BAR ? 0.4f : 0.55f);
					rvn_color_t accent = rvn_widget_color(widget, R_COLOR_ACCENT);
					accent = rvn_color_fade(accent, rvn_clampf(alpha * 1.35f + 0.14f, 0.0f, 0.78f) / rvn_maxf((float)rvn_color_a(accent) / 255.0f, 0.01f));
					tint = rvn_color_mix(rvn_color_fade(tint, rvn_clampf(alpha * 1.35f + 0.14f, 0.0f, 0.78f) / rvn_maxf(alpha, 0.01f)), accent, strength);
				}
				glass->saturation = 1.25f;
				break;
			default:
				break;
		}
		/* a palette made for solid surfaces would hide everything: glass keeps some of what is behind */
		alpha = (float)rvn_color_a(tint) / 255.0f;
		if (kind != RVN_SURFACE_ACCENT && alpha > 0.74f) tint = rvn_color_fade(tint, 0.74f / alpha);
	}
	glass->tint = tint;
	glass->rim = rvn_rgba(1.0f, 1.0f, 1.0f, rvn_clampf(rim + 0.25f * press, 0.0f, 1.0f));
	/* touched glass lights up */
	glass->glow = (dark ? 0.05f : 0.03f) * press;
	rvn_glass_tune(widget, glass);
}

void rvn_glass_tune(const rvn_widget_t* widget, rvn_glass_t* glass) {
	/* the glass of the theme (or of the widget): each metric multiplies what the material gives */
	float frost = rvn_widget_metric(widget, R_METRIC_GLASS_FROST);
	float refraction = rvn_widget_metric(widget, R_METRIC_GLASS_REFRACTION);
	float saturation = rvn_widget_metric(widget, R_METRIC_GLASS_SATURATION);
	float rim = rvn_widget_metric(widget, R_METRIC_GLASS_RIM);
	float shadow = rvn_widget_metric(widget, R_METRIC_GLASS_SHADOW);
	if (frost != 1.0f) glass->frost = rvn_clampf(glass->frost * frost, 0.0f, 1.0f);
	if (refraction != 1.0f) glass->refraction *= rvn_maxf(refraction, 0.0f);
	if (saturation != 1.0f) glass->saturation = rvn_maxf(0.0f, 1.0f + (glass->saturation - 1.0f) * saturation);
	if (rim != 1.0f) {
		uint32_t alpha = (uint32_t)rvn_clampf((float)rvn_color_a(glass->rim) * rvn_maxf(rim, 0.0f), 0.0f, 255.0f);
		glass->rim = (glass->rim & 0x00FFFFFFu) | (alpha << 24);
	}
	if (shadow != 1.0f) glass->shadow *= rvn_maxf(shadow, 0.0f);
}

void rvn_paint_surface(RPainter* painter, const rvn_widget_t* widget, rvn_rect_t rect, float radius,
	rvn_surface_t kind, float hover, float press) {
	rvn_glass_t glass;
	float radii[4];
	float opacity = painter->opacity;

	rvn_glass_style(widget, kind, hover, press, &glass);
	radii[0] = radii[1] = radii[2] = radii[3] = radius;
	if (!painter->enabled) {
		painter->opacity *= 0.5f;
		glass.shadow = 0.0f;
	}
	rvn_paint_glass(painter, rect, radii, &glass);
	painter->opacity = opacity;
}

void rvn_paint_row(RPainter* painter, const rvn_widget_t* widget, rvn_rect_t row, rvn_color_t color) {
	if (rvn_widget_glass(widget)) {
		rvn_paint_box(painter, rvn_rect(row.x + 4.0f, row.y + 1.0f, row.w - 8.0f, row.h - 2.0f), 8.0f, color, 0.0f, 0);
	} else {
		rvn_paint_rect(painter, row, color);
	}
}

float rvn_widget_hover(rvn_widget_t* widget, RPainter* painter, int hovered) {
	return rvn_paint_spring(painter, &widget->hover, hovered ? 1.0f : 0.0f, R_MOTION_SNAPPY);
}

float rvn_widget_press(rvn_widget_t* widget, RPainter* painter, int pressed) {
	/* goes down at once; released, it springs back past its rest position */
	return rvn_paint_spring(painter, &widget->press, pressed ? 1.0f : 0.0f, pressed ? R_MOTION_SNAPPY : R_MOTION_BOUNCY);
}

void rvn_widget_squash(rvn_widget_t* widget, float press, float amount) {
	/* pressed: flatter and wider, like something soft; released, the spring overshoots the other way */
	widget->xform.grow_x = amount * 0.45f * press;
	widget->xform.grow_y = -amount * press;
}

void rvn_paint_knob(RPainter* painter, const rvn_widget_t* widget, rvn_rect_t rect, float press) {
	float radius = rvn_minf(rect.w, rect.h) * 0.5f;
	if (press > 0.02f && painter->enabled) {
		/* picked up: the knob grows and turns into a drop of clear glass */
		float grow = 3.0f * press;
		rvn_rect_t lens = rvn_rect(rect.x - grow * 1.4f, rect.y - grow, rect.w + grow * 2.8f, rect.h + grow * 2.0f);
		float opacity = painter->opacity;
		rvn_paint_surface(painter, widget, lens, radius + grow, RVN_SURFACE_LENS, 0.0f, 1.0f);
		/* the solid knob fades into it */
		painter->opacity *= 1.0f - press;
		rvn_paint_box(painter, rect, radius, rvn_rgba(1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0);
		painter->opacity = opacity;
		return;
	}
	rvn_paint_shadow(painter, rvn_rect(rect.x, rect.y + 1.5f, rect.w, rect.h), radius, 5.0f, rvn_rgba(0.0f, 0.0f, 0.0f, 0.28f));
	rvn_paint_box(painter, rect, radius, painter->enabled ? rvn_rgba(1.0f, 1.0f, 1.0f, 1.0f) : rvn_rgba(0.85f, 0.85f, 0.87f, 1.0f), 0.0f, 0);
}

void rvn_paint_focus(const rvn_widget_t* widget, RPainter* painter, float radius) {
	float width = rvn_a11y_focus_width();
	if (!(widget->flags & RVN_WF_FOCUSED) || !widget->window) return;
	if (!widget->window->focus_visible && !rvn_a11y_focus_always()) return;
	rvn_paint_box(painter, rvn_rect(-width, -width, widget->rect.w + width * 2.0f, widget->rect.h + width * 2.0f), radius + width,
		0, width, rvn_widget_color(widget, R_COLOR_FOCUS));
}

/* ---------------------------------------------------------------------------
 *  Scrollbar
 * ------------------------------------------------------------------------- */

float rvn_scrollbar_max(const rvn_scrollbar_t* bar) {
	return rvn_maxf(0.0f, bar->content - bar->viewport);
}

int rvn_scrollbar_needed(const rvn_scrollbar_t* bar) {
	return bar->content > bar->viewport + 0.5f;
}

int rvn_scrollbar_set(rvn_scrollbar_t* bar, float offset) {
	float clamped = rvn_clampf(offset, 0.0f, rvn_scrollbar_max(bar));
	/* whole logical units keep scrolled content on the pixel grid at scale 1 */
	clamped = floorf(clamped + 0.5f);
	if (clamped > rvn_scrollbar_max(bar)) clamped = rvn_scrollbar_max(bar);
	if (clamped == bar->offset) return R_FALSE;
	bar->offset = clamped;
	return R_TRUE;
}

int rvn_scrollbar_clamp(rvn_scrollbar_t* bar) {
	return rvn_scrollbar_set(bar, bar->offset);
}

int rvn_scrollbar_wheel(rvn_scrollbar_t* bar, float delta) {
	if (rvn_scrollbar_set(bar, bar->offset + delta)) return R_TRUE;
	/* nowhere to go: the push becomes speed, and the spring brings the content back */
	if (rvn_scrollbar_needed(bar) && bar->shown.started) {
		rvn_spring_kick(&bar->shown, rvn_clampf(delta * 9.0f, -1400.0f, 1400.0f));
	}
	return R_FALSE;
}

float rvn_scrollbar_shown(rvn_scrollbar_t* bar, RPainter* painter) {
	/* a thumb that is dragged, or a view that lost its scrolling, follows at once */
	if (bar->dragging || !rvn_scrollbar_needed(bar)) rvn_spring_set(&bar->shown, bar->offset);
	return rvn_paint_spring(painter, &bar->shown, bar->offset, R_MOTION_SMOOTH);
}

static void rvn_scrollbar_thumb(const rvn_scrollbar_t* bar, float track_length, float* start, float* length) {
	float ratio = bar->content > 0.0f ? rvn_clampf(bar->viewport / bar->content, 0.0f, 1.0f) : 1.0f;
	float max = rvn_scrollbar_max(bar);
	*length = rvn_maxf(track_length * ratio, rvn_minf(24.0f, track_length));
	*start = max > 0.0f ? (track_length - *length) * (bar->offset / max) : 0.0f;
}

void rvn_scrollbar_paint(rvn_scrollbar_t* bar, const rvn_widget_t* widget, RPainter* painter,
	rvn_rect_t track, int vertical, float mouse_x, float mouse_y) {

	float start, length, thickness = vertical ? track.w : track.h;
	float track_length = vertical ? track.h : track.w, max = rvn_scrollbar_max(bar);
	float shown = bar->shown.started ? bar->shown.value : bar->offset;
	rvn_rect_t thumb;
	int hot;

	if (!rvn_scrollbar_needed(bar)) return;
	rvn_scrollbar_thumb(bar, track_length, &start, &length);
	/* the thumb follows what is drawn, and is squeezed against the end the content is pulled past */
	if (max > 0.0f) start = (track_length - length) * rvn_clampf(shown / max, 0.0f, 1.0f);
	if (shown < 0.0f) {
		length = rvn_maxf(length + shown, 12.0f);
	} else if (shown > max) {
		float squeezed = rvn_maxf(length - (shown - max), 12.0f);
		start += length - squeezed;
		length = squeezed;
	}
	if (vertical) thumb = rvn_rect(track.x + 2.0f, track.y + start + 2.0f, thickness - 4.0f, length - 4.0f);
	else thumb = rvn_rect(track.x + start + 2.0f, track.y + 2.0f, length - 4.0f, thickness - 4.0f);

	hot = bar->dragging || ((widget->flags & RVN_WF_HOVERED) && rvn_rect_contains(track, mouse_x, mouse_y));
	if (rvn_widget_glass(widget)) {
		/* no track: a thin capsule over the content, thicker under the pointer, gone when nothing moves */
		int active = hot || (widget->flags & RVN_WF_HOVERED) || rvn_spring_moving(&bar->shown, bar->offset);
		float visible = rvn_clampf(rvn_paint_spring(painter, &bar->fade, active ? 1.0f : 0.0f, R_MOTION_SMOOTH), 0.0f, 1.0f);
		float slim = hot ? 1.0f : 2.0f;
		if (visible < 0.02f) return;
		if (vertical) thumb = rvn_rect(thumb.x + slim, thumb.y, thumb.w - slim * 2.0f, thumb.h);
		else thumb = rvn_rect(thumb.x, thumb.y + slim, thumb.w, thumb.h - slim * 2.0f);
		rvn_paint_box(painter, thumb, thickness,
			rvn_color_fade(rvn_widget_color(widget, hot ? R_COLOR_HANDLE_HOVERED : R_COLOR_HANDLE), visible), 0.0f, 0);
		return;
	}
	rvn_paint_rect(painter, track, rvn_color_fade(rvn_widget_color(widget, R_COLOR_TRACK), 0.6f));
	rvn_paint_box(painter, thumb, (thickness - 4.0f) * 0.5f,
		rvn_widget_color(widget, hot ? R_COLOR_HANDLE_HOVERED : R_COLOR_HANDLE), 0.0f, 0);
}

int rvn_scrollbar_event(rvn_scrollbar_t* bar, rvn_widget_t* widget, const REvent* event,
	rvn_rect_t track, int vertical, int* changed) {

	float track_length = vertical ? track.h : track.w;
	float position = vertical ? event->local_y - track.y : event->local_x - track.x;
	float start, length, travel;

	*changed = R_FALSE;
	if (!bar->dragging && !rvn_scrollbar_needed(bar)) return R_FALSE;
	rvn_scrollbar_thumb(bar, track_length, &start, &length);
	travel = track_length - length;

	switch (event->type) {
		case R_EVENT_MOUSE_DOWN:
			if (event->button != R_MOUSE_LEFT || !rvn_rect_contains(track, event->local_x, event->local_y)) return R_FALSE;
			if (position < start || position > start + length) {
				/* click in the track: bring the middle of the thumb under the cursor */
				if (travel > 0.0f) *changed = rvn_scrollbar_set(bar, (position - length * 0.5f) / travel * rvn_scrollbar_max(bar));
				rvn_scrollbar_thumb(bar, track_length, &start, &length);
			}
			bar->dragging = R_TRUE;
			bar->grab = position - start;
			rvn_widget_capture_mouse(widget);
			return R_TRUE;
		case R_EVENT_MOUSE_MOVE:
			if (!bar->dragging) return R_FALSE;
			if (travel > 0.0f) *changed = rvn_scrollbar_set(bar, (position - bar->grab) / travel * rvn_scrollbar_max(bar));
			return R_TRUE;
		case R_EVENT_MOUSE_UP:
			if (!bar->dragging || event->button != R_MOUSE_LEFT) return R_FALSE;
			bar->dragging = R_FALSE;
			rvn_widget_release_mouse(widget);
			return R_TRUE;
		default:
			return R_FALSE;
	}
}
