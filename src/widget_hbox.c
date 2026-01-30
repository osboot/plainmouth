// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

static void hbox_measure(struct widget *w) __attribute__((nonnull(1)));
static void distribute_flex_hbox(int count, const int *pref,
		const int *min, const int *max, const int *grow,
		const int *shrink, int available, int *out) __attribute__((nonnull(2,3,4,5,6,8)));
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

/* Iterative distribution helper for one axis (HBOX main axis) */
void distribute_flex_hbox(int count, const int *pref,
		const int *min, const int *max, const int *grow,
		const int *shrink, int available, int *out)
{
	int i;

	/* sum preferred */
	int sum_pref = 0;
	for (i = 0; i < count; i++)
		sum_pref += pref[i];

	if (available >= sum_pref) {
		/* Grow case */
		int extra = available - sum_pref;
		int sum_grow = 0;

		for (i = 0; i < count; i++)
			sum_grow += grow[i];

		int allocated = 0;
		for (i = 0; i < count; i++) {
			int add = (sum_grow > 0) ? (extra * grow[i]) / sum_grow : 0;

			out[i] = pref[i] + add;
			allocated += add;

			/* respect max */
			if (max[i] > 0 && out[i] > max[i])
				out[i] = max[i];
		}

		/* distribute remainder (rounding) and respect max */
		int rem = extra - allocated;

		for (i = 0; i < count && rem > 0; i++) {
			if (!grow[i])
				continue;

			if (max[i] == 0 || out[i] < max[i]) {
				out[i]++;
				rem--;
			}
		}
		return;
	}

	/* Shrink case: iterative clamp to min */
	int deficit = sum_pref - available;

	for (i = 0; i < count; i++)
		out[i] = pref[i];

	bool changed = true;

	while (deficit > 0 && changed) {
		int sum_shrink_active = 0;

		changed = false;

		for (i = 0; i < count; i++) {
			if (out[i] > min[i])
				sum_shrink_active += shrink[i];
		}

		if (sum_shrink_active == 0)
			break;

		int total_cut = 0;

		for (i = 0; i < count; i++) {
			if (out[i] <= min[i])
				continue;

			int cut = (deficit * shrink[i]) / sum_shrink_active;
			int newsize = out[i] - cut;

			if (newsize < min[i])
				newsize = min[i];

			total_cut += (out[i] - newsize);

			if (newsize != out[i])
				changed = true;

			out[i] = newsize;
		}
		deficit -= total_cut;
	}

	/*
	 * If deficit still > 0, force-last-resort trim from rightmost children
	 */
	for (i = count - 1; i >= 0 && deficit > 0; i--) {
		int take = MIN(deficit, out[i] - min[i]);
		if (take > 0) {
			out[i] -= take;
			deficit -= take;
		}
	}
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

	distribute_flex_hbox(count, pref, min, max, grow, shrink, w->w, out);

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
	.free_data        = NULL,
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
