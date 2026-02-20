// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "widget.h"

static void vscroll_measure(struct widget *w) __attribute__((nonnull(1)));
static void vscroll_render(struct widget *w) __attribute__((nonnull(1)));
static bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static bool vscroll_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));


void vscroll_measure(struct widget *w)
{
	widget_scrollbar_measure(w, true);
}

void vscroll_render(struct widget *w)
{
	widget_scrollbar_render(w, true);
}

bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	return widget_scrollbar_setter(w->state, prop, in,
			PROP_SCROLL_CONTENT_H, PROP_SCROLL_VIEW_H, PROP_SCROLL_Y);
}

bool vscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	return widget_scrollbar_getter(w->state, prop, out, PROP_SCROLL_Y);
}

static const struct widget_ops vscroll_ops = {
	.measure          = vscroll_measure,
	.layout           = NULL,
	.render           = vscroll_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = widget_scrollbar_state_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = vscroll_setter,
	.getter           = vscroll_getter,
	.getter_index     = NULL,
};

struct widget *make_vscroll(void)
{
	struct widget *w = widget_create(WIDGET_VSCROLL);
	if (!w)
		return NULL;

	struct widget_scrollbar_state *s = calloc(1, sizeof(*s));
	if (!s) {
		warn("make_vscroll: calloc");
		widget_free(w);
		return NULL;
	}

	w->state = s;

	w->ops = &vscroll_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 1;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
