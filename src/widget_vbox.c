// SPDX-License-Identifier: GPL-2.0-or-later

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

static void distribute_flex_vbox(int count, const int *pref,
		const int *min, const int *max, const int *grow,
		const int *shrink, int available, int *out)
{
	int i;
	int sum_pref = 0;

	for (i = 0; i < count; i++)
		sum_pref += pref[i];

	if (available >= sum_pref) {
		int extra = available - sum_pref;
		int sum_grow = 0;

		for (i = 0; i < count; i++)
			sum_grow += grow[i];

		int allocated = 0;

		for (i = 0; i < count; i++) {
			int add = (sum_grow > 0) ? (extra * grow[i]) / sum_grow : 0;

			out[i] = pref[i] + add;
			allocated += add;

			if (max[i] > 0 && out[i] > max[i])
				out[i] = max[i];
		}

		int rem = extra - allocated;

		for (i = 0; i < count && rem > 0; i++) {
			if (max[i] == 0 || out[i] < max[i]) {
				out[i]++;
				rem--;
			}
		}
		return;
	}

	int deficit = sum_pref - available;

	for (i = 0; i < count; i++)
		out[i] = pref[i];

	bool changed = true;

	while (deficit > 0 && changed) {
		int sum_shrink_active = 0;

		changed = false;

		for (i = 0; i < count; i++)
			if (out[i] > min[i])
				sum_shrink_active += shrink[i];

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

	for (i = count - 1; i >= 0 && deficit > 0; i--) {
		int take = MIN(deficit, out[i] - min[i]);
		if (take > 0) {
			out[i] -= take;
			deficit -= take;
		}
	}
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

	distribute_flex_vbox(count, pref, min, max, grow, shrink, w->h, out);

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

struct widget *make_vbox(void)
{
	struct widget *w = widget_create(WIDGET_VBOX);

	w->measure    = vbox_measure;
	w->layout     = vbox_layout;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = 1;
	w->flex_w = 1;

	/* VBOX normally stretches horizontally to fill parent width */
	w->stretch_w = true;

	return w;
}
