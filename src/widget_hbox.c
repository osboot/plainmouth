// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

/*
 * Compute horizontal container minimum size:
 *   min_w = sum of child min_w
 *   min_h = max child min_h
 */
static void hbox_measure(struct widget *w)
{
	struct widget *c;

	int sum_min_w = 0;
	int max_h = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		sum_min_w += c->min_w;
		max_h = MAX(max_h, c->min_h);
	}

	w->min_w = sum_min_w;
	w->min_h = max_h;
}

static void hbox_layout0(struct widget *w)
{
	struct widget *c;
	int x = 0;

	int total_fixed = 0;
	int total_flex = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->flex_w > 0)
			total_flex += c->flex_w;
		else
			total_fixed += c->min_w;
	}

	int remaining = w->w - total_fixed;

	if (remaining < 0)
		remaining = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int c_w = (c->flex_w > 0 && total_flex > 0)
				? (remaining * c->flex_w) / total_flex
				: c->min_w;

		if (c_w < c->min_w)
			c_w = c->min_w;

		if ((x + c_w) > w->w)
			c_w = MAX(0, w->w - x);

		widget_layout_tree(c, x, 0, c_w, w->h);
		x += c_w;
	}
}

static void hbox_layout(struct widget *w)
{
	int total_min = 0;
	int total_flex = 0;
	int total_shrink = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		total_min += c->min_w;
		total_flex += c->flex_w;
		total_shrink += c->shrink_w;
	}

	int free_space = w->w - total_min;
	int x = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int cw = c->min_w;

		if (free_space > 0 && total_flex > 0) {
			int extra = (free_space * c->flex_w) / total_flex;
			cw = c->min_w + extra;

		} else if (free_space < 0 && total_shrink > 0) {
			int deficit = -free_space;
			int cut = (deficit * c->shrink_w) / total_shrink;
			cw = MAX(0, c->min_w - cut);
		}

		 int ch = c->grow_h > 0 ? w->h : c->min_h;

		widget_layout_tree(c, x, 0, cw, ch);
		x += cw;
	}
}

struct widget *make_hbox(void)
{
	struct widget *w = widget_create(WIDGET_HBOX);

	w->measure    = hbox_measure;
	w->layout     = hbox_layout;
	w->render     = NULL;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = w->flex_w = 1;

	return w;
}
