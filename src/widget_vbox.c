// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

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

void vbox_layout(struct widget *w)
{
	struct widget *c;
	int y;

	int count = 0;
	TAILQ_FOREACH(c, &w->children, siblings)
		count++;

	if (count == 0)
		return;

	int *pref   __free(ptr) = calloc((size_t)count, sizeof(int));
	int *min    __free(ptr) = calloc((size_t)count, sizeof(int));
	int *max    __free(ptr) = calloc((size_t)count, sizeof(int));
	int *grow   __free(ptr) = calloc((size_t)count, sizeof(int));
	int *shrink __free(ptr) = calloc((size_t)count, sizeof(int));
	int *out    __free(ptr) = calloc((size_t)count, sizeof(int));

	if (!pref || !min || !grow || !shrink || !out || !max)
		errx(EXIT_FAILURE, "vbox_layout: calloc failed");

	int i = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		pref[i]   = (c->pref_h > 0) ? c->pref_h : c->min_h;
		min[i]    = c->min_h;
		max[i]    = c->max_h;
		grow[i]   = c->flex_h;
		shrink[i] = c->shrink_h;
		i++;
	}

	distribute_flex_axis(count, pref, min, max, grow, shrink, w->h, out);

	/* apply */
	y = 0;
	i = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = out[i];
		int cw = c->stretch_w ? w->w : c->min_w;
		widget_layout_tree(c, 0, y, cw, ch);
		y += ch;
		i++;
	}
}

static const struct widget_ops vbox_ops = {
	.measure          = vbox_measure,
	.layout           = vbox_layout,
	.render           = NULL,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = NULL,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = NULL,
	.getter           = NULL,
	.getter_index     = NULL,
};

struct widget *make_vbox(void)
{
	struct widget *w = widget_create(WIDGET_VBOX);

	w->ops = &vbox_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = 1;
	w->flex_w = 1;

	/* VBOX normally stretches horizontally to fill parent width */
	w->stretch_w = true;

	return w;
}
