// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

struct widget_spinbox {
	int min;
	int max;
	int step;
	int value;

	int width;
	int edit_buf;
	int edit_len;
};

static int spinbox_clamp(int v, int min, int max);
static void spinbox_commit(struct widget_spinbox *s) __attribute__((nonnull(1)));
static void spinbox_measure(struct widget *w) __attribute__((nonnull(1)));
static void spinbox_render(struct widget *w) __attribute__((nonnull(1)));
static int spinbox_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool spinbox_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static void spinbox_free(struct widget *w);


int spinbox_clamp(int v, int min, int max)
{
	if (v < min) return min;
	if (v > max) return max;
	return v;
}

void spinbox_commit(struct widget_spinbox *s)
{
	s->value = spinbox_clamp(s->edit_buf, s->min, s->max);
	s->edit_buf = 0;
	s->edit_len = 0;
}

void spinbox_measure(struct widget *w)
{
	struct widget_spinbox *st = w->state;

	w->pref_h = w->min_h = 1;
	w->pref_w = w->min_w = st->width + 2; // "[00]"
}

void spinbox_render(struct widget *w)
{
	struct widget_spinbox *st = w->state;
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	werase(w->win);
	wbkgd(w->win, COLOR_PAIR(color));

	w_mvprintw(w->win, 0, 0, L"[%0*d]", st->width, st->value);

	wnoutrefresh(w->win);
}

void spinbox_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

int spinbox_input(const struct widget *w, wchar_t key)
{
	struct widget_spinbox *st = w->state;

	switch (key) {
		case KEY_UP:
			st->value = spinbox_clamp(st->value + st->step, st->min, st->max);
			return 1;

		case KEY_DOWN:
			st->value = spinbox_clamp(st->value - st->step, st->min, st->max);
			return 1;

		case KEY_BACKSPACE:
		case 127:
			st->edit_buf = 0;
			st->edit_len = 0;
			return 1;

		default:
			break;
	}

	if (key >= L'0' && key <= L'9') {
		st->edit_buf = (st->edit_buf * 10) + (key - L'0');
		st->edit_len++;

		if (st->edit_len >= st->width)
			spinbox_commit(st);

		return 1;
	}

	return 0;
}

bool spinbox_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_spinbox *st = w->state;

	if (prop == PROP_SPINBOX_VALUE) {
		*(int *) out = st->value;
		return true;
	}
	return false;
}

bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_spinbox *st = w->state;

	if (prop == PROP_SPINBOX_VALUE) {
		st->value = spinbox_clamp(*(const int *) in, st->min, st->max);
		return true;
	}
	return false;
}

static const struct widget_ops spinbox_ops = {
	.measure          = spinbox_measure,
	.layout           = NULL,
	.render           = spinbox_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = spinbox_free,
	.input            = spinbox_input,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = spinbox_setter,
	.getter           = spinbox_getter,
	.getter_index     = NULL,
};

struct widget *make_spinbox(int min, int max, int step, int initial, int width)
{
	struct widget *w = widget_create(WIDGET_SPINBOX);
	if (!w)
		return NULL;

	struct widget_spinbox *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_spinbox: calloc");
		widget_free(w);
		return NULL;
	}

	state->min   = min;
	state->max   = max;
	state->step  = MAX(1, step);
	state->width = MAX(1, width);
	state->value = spinbox_clamp(initial, min, max);

	w->state = state;
	w->ops = &spinbox_ops;
	w->color_pair    = COLOR_PAIR_WINDOW;
	w->attrs         = ATTR_CAN_FOCUS;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false;
	w->stretch_h = false;

	return w;
}
