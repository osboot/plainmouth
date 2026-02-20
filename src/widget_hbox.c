// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

static void hbox_measure(struct widget *w) __attribute__((nonnull(1)));
static void hbox_layout(struct widget *w) __attribute__((nonnull(1)));

/*
 * Compute horizontal container minimum size:
 *   min_w = sum of child min_w
 *   min_h = max child min_h
 */
void hbox_measure(struct widget *w)
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

void hbox_layout(struct widget *w)
{
	struct widget *c;
	int x;

	int count = 0;
	TAILQ_FOREACH(c, &w->children, siblings)
		count++;

	if (!count)
		return;

	int *pref   __free(ptr) = calloc((size_t)count, sizeof(int));
	int *min    __free(ptr) = calloc((size_t)count, sizeof(int));
	int *max    __free(ptr) = calloc((size_t)count, sizeof(int));
	int *grow   __free(ptr) = calloc((size_t)count, sizeof(int));
	int *shrink __free(ptr) = calloc((size_t)count, sizeof(int));
	int *out    __free(ptr) = calloc((size_t)count, sizeof(int));

	if (!pref || !min || !grow || !shrink || !out || !max)
		errx(EXIT_FAILURE, "hbox_layout: calloc failed");

	int i = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		pref[i]   = (c->pref_w > 0) ? c->pref_w : c->min_w;
		min[i]    = c->min_w;
		max[i]    = c->max_w;
		grow[i]   = c->flex_w;
		shrink[i] = c->shrink_w;
		i++;
	}

	distribute_flex_axis(count, pref, min, max, grow, shrink, w->w, out);

	/* apply results */
	x = 0;
	i = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int cw = out[i];
		int ch = c->stretch_h ? w->h : c->min_h;
		widget_layout_tree(c, x, 0, cw, ch);
		x += cw;
		i++;
	}
}

static const struct widget_ops hbox_ops = {
	.measure          = hbox_measure,
	.layout           = hbox_layout,
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

struct widget *make_hbox(void)
{
	struct widget *w = widget_create(WIDGET_HBOX);

	w->ops = &hbox_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = 1;
	w->flex_w = 1;

	/* HBOX should expand horizontally when placed in a vertical container */
	w->stretch_w = true;

	return w;
}
