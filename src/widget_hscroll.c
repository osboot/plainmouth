// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "widget.h"

static void hscroll_measure(struct widget *w) __attribute__((nonnull(1)));
static void hscroll_render(struct widget *w) __attribute__((nonnull(1)));
static bool hscroll_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static bool hscroll_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));

void hscroll_measure(struct widget *w)
{
	widget_scrollbar_measure(w, false);
}

void hscroll_render(struct widget *w)
{
	widget_scrollbar_render(w, false);
}

bool hscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	return widget_scrollbar_setter(w->state, prop, in,
			PROP_SCROLL_CONTENT_W, PROP_SCROLL_VIEW_W, PROP_SCROLL_X);
}

bool hscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	return widget_scrollbar_getter(w->state, prop, out, PROP_SCROLL_X);
}

static const struct widget_ops hscroll_ops = {
	.measure          = hscroll_measure,
	.layout           = NULL,
	.render           = hscroll_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = widget_scrollbar_state_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = hscroll_setter,
	.getter           = hscroll_getter,
	.getter_index     = NULL,
};

struct widget *make_hscroll(void)
{
	struct widget *w = widget_create(WIDGET_HSCROLL);
	if (!w)
		return NULL;

	struct widget_scrollbar_state *s = calloc(1, sizeof(*s));
	if (!s) {
		warn("make_hscroll: calloc");
		widget_free(w);
		return NULL;
	}

	w->state   = s;
	w->ops     = &hscroll_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 0;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
