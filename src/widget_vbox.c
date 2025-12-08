// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

/*
 * Compute vertical container minimum size:
 *   min_w = max child min_w
 *   min_h = sum of child min_h
 */
void vbox_measure(struct widget *w)
{
	struct widget *c;
	int sum_min_h = 0;
	int max_w = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		sum_min_h += c->min_h;
		max_w = MAX(max_w, c->min_w);
	}

	w->min_h = sum_min_h;
	w->min_w = max_w;
}

void vbox_layout0(struct widget *w)
{
	struct widget *c;
	int y = 0;

	/* distribute vertical space */
	int total_fixed = 0;
	int total_flex = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->flex_h > 0)
			total_flex += c->flex_h;
		else
			total_fixed += c->min_h;
	}

	int remaining = w->h - total_fixed;

	if (remaining < 0)
		remaining = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int c_h = (c->flex_h > 0 && total_flex > 0)
				? (remaining * c->flex_h) / total_flex
				: c->min_h;

		if (c_h < c->min_h)
			c_h = c->min_h;

		if ((y + c_h) > w->h)
			c_h = MAX(0, w->h - y);

		widget_layout_tree(c, 0, y, w->w, c_h);
		y += c_h;
	}
}

void vbox_layout(struct widget *w)
{
	int total_min = 0;
	int total_flex = 0;
	int total_shrink = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		total_min += c->min_h;
		total_flex += c->flex_h;
		total_shrink += c->shrink_h;
	}

	int free_space = w->h - total_min;
	int y = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = c->min_h;

		if (free_space > 0 && total_flex > 0) {
			/* grow */
			int extra = (free_space * c->flex_h) / total_flex;
			ch = c->min_h + extra;

		} else if (free_space < 0 && total_shrink > 0) {
			/* shrink */
			int deficit = -free_space;
			int cut = (deficit * c->shrink_h) / total_shrink;
			ch = MAX(0, c->min_h - cut);
		}

		int cw = c->grow_w > 0 ? w->w : c->min_w;

		widget_layout_tree(c, 0, y, cw, ch);
		y += ch;
	}
}

struct widget *make_vbox(void)
{
	struct widget *w = widget_create(WIDGET_VBOX);

	w->measure    = vbox_measure;
	w->layout     = vbox_layout;
	w->render     = NULL;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = w->flex_w = 1;

	return w;
}
