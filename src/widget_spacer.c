// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include "widget.h"

static void spacer_measure(struct widget *w)
{
	w->min_w = w->pref_w;
	w->min_h = w->pref_h;
}

static const struct widget_ops spacer_ops = {
	.measure = spacer_measure,
};

struct widget *make_spacer(int width, int height)
{
	if (width < 0 || height < 0)
		return NULL;

	struct widget *w = widget_create(WIDGET_SPACER);

	if (!w)
		return NULL;

	w->ops = &spacer_ops;
	w->pref_w = width;
	w->pref_h = height;
	w->stretch_w = true;
	w->stretch_h = true;
	w->color_pair = COLOR_PAIR_WINDOW;
	return w;
}
