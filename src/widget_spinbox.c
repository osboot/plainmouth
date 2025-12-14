// SPDX-License-Identifier: GPL-2.0-or-later

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
static void spinbox_free(struct widget *w) __attribute__((nonnull(1)));
static int spinbox_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool spinbox_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));


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
	struct widget_spinbox *s = w->state.spinbox;

	w->pref_h = w->min_h = 1;
	w->pref_w = w->min_w = s->width + 2; // "[00]"
}

void spinbox_render(struct widget *w)
{
	struct widget_spinbox *s = w->state.spinbox;
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	werase(w->win);
	wbkgd(w->win, COLOR_PAIR(color));

	mvwprintw(w->win, 0, 0, "[%0*d]", s->width, s->value);

	wnoutrefresh(w->win);
}

void spinbox_free(struct widget *w)
{
	if (w && w->state.spinbox)
		free(w->state.spinbox);
}

int spinbox_input(const struct widget *w, wchar_t key)
{
	struct widget_spinbox *s = w->state.spinbox;

	switch (key) {
		case KEY_UP:
			s->value = spinbox_clamp(s->value + s->step, s->min, s->max);
			return 1;

		case KEY_DOWN:
			s->value = spinbox_clamp(s->value - s->step, s->min, s->max);
			return 1;

		case KEY_BACKSPACE:
		case 127:
			s->edit_buf = 0;
			s->edit_len = 0;
			return 1;

		default:
			break;
	}

	if (key >= L'0' && key <= L'9') {
		s->edit_buf = (s->edit_buf * 10) + (key - L'0');
		s->edit_len++;

		if (s->edit_len >= s->width)
			spinbox_commit(s);

		return 1;
	}

	return 0;
}

bool spinbox_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_spinbox *s = w->state.spinbox;

	if (prop == PROP_SPINBOX_VALUE) {
		*(int *) out = s->value;
		return true;
	}
	return false;
}

bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_spinbox *s = w->state.spinbox;

	if (prop == PROP_SPINBOX_VALUE) {
		s->value = spinbox_clamp(*(const int *) in, s->min, s->max);
		return true;
	}
	return false;
}

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

	w->state.spinbox = state;
	w->measure       = spinbox_measure;
	w->render        = spinbox_render;
	w->input         = spinbox_input;
	w->free_data     = spinbox_free;
	w->getter        = spinbox_getter;
	w->setter        = spinbox_setter;
	w->color_pair    = COLOR_PAIR_WINDOW;
	w->attrs         = ATTR_CAN_FOCUS;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false;
	w->stretch_h = false;

	return w;
}
