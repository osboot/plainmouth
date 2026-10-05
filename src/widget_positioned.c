// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <limits.h>

#include "widget.h"

/* Child geometry is explicit and remains independent of the viewport size. */
static void positioned_measure(struct widget *w)
{
	w->min_w = w->min_h = 1;
	struct widget *child;
	TAILQ_FOREACH(child, &w->children, siblings)
	{
		if (child->lx + child->w > w->min_w)
			w->min_w = child->lx + child->w;
		if (child->ly + child->h > w->min_h)
			w->min_h = child->ly + child->h;
	}
	w->pref_w = w->min_w;
	w->pref_h = w->min_h;
}

static void positioned_layout(struct widget *w)
{
	struct widget *child;
	TAILQ_FOREACH(child, &w->children, siblings)
	widget_layout_tree(child, child->lx, child->ly, child->w, child->h);
}

static const struct widget_ops positioned_ops = {
	.measure = positioned_measure,
	.layout = positioned_layout,
};

struct widget *make_positioned(void)
{
	struct widget *w = widget_create(WIDGET_POSITIONED);
	if (w) {
		w->ops = &positioned_ops;
		w->color_pair = COLOR_PAIR_WINDOW;
	}
	return w;
}

bool positioned_add(struct widget *parent, struct widget *child, int x, int y, int width, int height)
{
	if (!parent || parent->type != WIDGET_POSITIONED || !child || child->parent ||
	    x < 0 || y < 0 || width <= 0 || height <= 0 ||
	    x > INT_MAX - width || y > INT_MAX - height)
		return false;
	child->lx = x;
	child->ly = y;
	child->w = width;
	child->h = height;
	widget_add(parent, child);
	return true;
}
