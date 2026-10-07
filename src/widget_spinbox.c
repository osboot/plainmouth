// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
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
	long long edit_buf;
	int edit_len;
	bool negative;
};

static int spinbox_clamp(long long v, int min, int max);
static void spinbox_commit(struct widget_spinbox *s) __attribute__((nonnull(1)));
static void spinbox_measure(struct widget *w) __attribute__((nonnull(1)));
static void spinbox_render(struct widget *w) __attribute__((nonnull(1)));
static int spinbox_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool spinbox_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static void spinbox_free(struct widget *w);


int spinbox_clamp(long long v, int min, int max)
{
	if (v < min) return min;
	if (v > max) return max;
	return (int) v;
}

static void spinbox_reset_edit(struct widget_spinbox *s)
{
	s->edit_buf = 0;
	s->edit_len = 0;
	s->negative = false;
}

void spinbox_commit(struct widget_spinbox *s)
{
	long long value = s->edit_buf;

	if (s->negative)
		value = -value;

	s->value = spinbox_clamp(value, s->min, s->max);
	spinbox_reset_edit(s);
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
	widget_style_apply_widget(w, color);

	if (st->negative)
		w_mvprintw(w->win, 0, 0, L"[-%0*lld]", MAX(1, st->width - 1), st->edit_buf);
	else if (st->edit_len)
		w_mvprintw(w->win, 0, 0, L"[%0*lld]", st->width, st->edit_buf);
	else
		w_mvprintw(w->win, 0, 0, L"[%0*d]", st->width, st->value);

	widget_noutrefresh(w);
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
			spinbox_reset_edit(st);
			st->value = spinbox_clamp((long long) st->value + st->step, st->min, st->max);
			return 1;

		case KEY_DOWN:
			spinbox_reset_edit(st);
			st->value = spinbox_clamp((long long) st->value - st->step, st->min, st->max);
			return 1;

		case KEY_BACKSPACE:
		case 127:
			spinbox_reset_edit(st);
			return 1;

		case KEY_ENTER:
		case L'\n':
		case L'\r':

			if (st->edit_len)
				spinbox_commit(st);

			return 1;

		default:
			break;
	}

	if (key == L'-' && st->min < 0) {
		spinbox_reset_edit(st);
		st->negative = true;
		return 1;
	}

	if (key >= L'0' && key <= L'9') {
		long long limit = INT_MAX;

		if (st->negative)
			limit++;

		st->edit_buf = MIN((st->edit_buf * 10) + (key - L'0'), limit);
		st->edit_len++;

		int digits = st->width;

		if (st->negative)
			digits--;

		if (st->edit_len >= digits)
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

	if (prop == PROP_SPINBOX_MIN) {
		*(int *) out = st->min;
		return true;
	}

	if (prop == PROP_SPINBOX_MAX) {
		*(int *) out = st->max;
		return true;
	}

	return false;
}

bool spinbox_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_spinbox *st = w->state;

	if (prop == PROP_SPINBOX_VALUE) {
		spinbox_reset_edit(st);
		st->value = spinbox_clamp(*(const int *) in, st->min, st->max);
		return true;
	}
	return false;
}

static size_t spinbox_keybindings(const struct widget *w, const struct widget_keybinding **bindings)
{
	static const struct widget_keybinding keys[] = {
		{ KEY_UP,        true,  "Up",        "Increase value"        },
		{ KEY_DOWN,      true,  "Down",      "Decrease value"        },
		{ KEY_BACKSPACE, true,  "Backspace", "Clear pending digits"  },
		{ 0,             false, "0-9",       "Enter numeric value"   },
		{ L'\n',         false, "Enter",     "Confirm numeric value" },
		{ L'-',          false, "-",         "Start negative value"  },
	};
	*bindings = keys;
	size_t count = sizeof(keys) / sizeof(*keys);
	const struct widget_spinbox *st = w->state;

	if (st->min >= 0)
		count--;

	return count;
}

static const struct widget_ops spinbox_ops = {
	.keybindings      = spinbox_keybindings,
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
