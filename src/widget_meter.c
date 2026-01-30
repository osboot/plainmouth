// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

struct widget_meter {
	bool percent;
	int total;
	int value;
};

static void show_percent(WINDOW *win, struct widget_meter *state) __attribute__((nonnull(1,2)));
static void meter_measure(struct widget *w) __attribute__((nonnull(1)));
static void meter_render(struct widget *w) __attribute__((nonnull(1)));
static bool meter_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1,3)));
static bool meter_setter(struct widget *w, enum widget_property prop, const void *value) __attribute__((nonnull(1,3)));
static void meter_free(struct widget *w);

void show_percent(WINDOW *win, struct widget_meter *state)
{
	int max_x = getmaxx(win);

	if (max_x < 4)
		return;

	w_mvprintw(win, 0, (max_x / 2) - 1,
		   L"%3d%%", simple_round(((float) state->value * 100) / (float) state->total));
}

void meter_measure(struct widget *w)
{
	w->min_h = 1;
	w->min_w = 1;
}

void meter_render(struct widget *w)
{
	struct widget_meter *st = w->state;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	wmove(w->win, 0, 0);
	wclrtoeol(w->win);

	int filled = (w->w * st->value) / st->total;

	/* filled area */
	wattron(w->win, A_REVERSE);
	for (int i = 0; i < filled; i++)
		mvwaddch(w->win, 0, i, ACS_CKBOARD);
	wattroff(w->win, A_REVERSE);

	show_percent(w->win, st);
	wnoutrefresh(w->win);
}

void meter_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

bool meter_getter(struct widget *w, enum widget_property prop, void *data)
{
	struct widget_meter *st = w->state;

	if (prop == PROP_METER_TOTAL) {
		*(int *) data = st->total;
		return true;

	} else if (prop == PROP_METER_VALUE) {
		*(int *) data = st->value;
		return true;

	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

bool meter_setter(struct widget *w, enum widget_property prop, const void *data)
{
	struct widget_meter *st = w->state;

	if (prop == PROP_METER_VALUE) {
		int value = *((const int *) data);

		if (value < 0)
			value = 0;

		if (value > st->total)
			value = st->total;

		st->value = value;
		return true;

	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

static const struct widget_ops meter_ops = {
	.measure          = meter_measure,
	.layout           = NULL,
	.render           = meter_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free_data        = meter_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = meter_setter,
	.getter           = meter_getter,
	.getter_index     = NULL,
};

struct widget *make_meter(int total)
{
	struct widget *w = widget_create(WIDGET_METER);
	if (!w)
		return NULL;

	struct widget_meter *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_button: calloc");
		widget_free(w);
		return NULL;
	}

	state->value = 0;
	state->total = total;

	w->state = state;
	w->ops = &meter_ops;
	w->color_pair  = COLOR_PAIR_WINDOW;

	w->flex_w = 1;        /* expand horizontally */
	w->flex_h = 0;        /* fixed height */

	w->stretch_w = true;  /* fill horizontal space */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
