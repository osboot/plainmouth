// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

struct widget_input {
	wchar_t force_chr;
	wchar_t *placeholder;

	wchar_t *text;
	int cap;
	int len;
	int max_length;

	int cursor_x;
	int cursor_y;
	int index;
	int view_start;

	bool finished;
	bool finish_on_enter;
};

static void input_measure(struct widget *w) __attribute__((nonnull(1)));
static void input_render(struct widget *w) __attribute__((nonnull(1)));
static int input_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool input_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1, 3)));
static bool input_setter(struct widget *w, enum widget_property prop, const void *value) __attribute__((nonnull(1, 3)));
static void input_free(struct widget *w);
static bool __input_unchr(struct widget_input *state) __attribute__((nonnull(1)));
static bool __input_append(struct widget_input *state, wchar_t c) __attribute__((nonnull(1)));

void input_measure(struct widget *w)
{
	struct widget_input *st = w->state;

	/* Minimum workable size */
	w->min_h = 1;
	w->min_w = 1;

	/* Preferred size = size of placeholder or existing text */
	if (st->placeholder) {
		int len = (int) wcslen(st->placeholder);
		w->pref_w = MAX(w->pref_w, len);
	}

	w->pref_w = MAX(w->pref_w, st->len);
	w->pref_h = 1;

	/* Max size: unlimited unless user overrides via API later */
	w->max_w = INT_MAX;
	w->max_h = 1;
}

static int input_char_width(struct widget_input *st, int index)
{
	int width = wcwidth(st->force_chr ?: st->text[index]);
	return MAX(0, width);
}

void input_render(struct widget *w)
{
	struct widget_input *st = w->state;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	widget_style_apply_widget(w, color);

	wmove(w->win, 0, 0);
	wclrtoeol(w->win);

	st->view_start = MIN(st->view_start, st->index);

	long columns = 0;

	for (int i = st->view_start; i < st->index; i++)
		columns += input_char_width(st, i);

	while (columns > w->w && st->view_start < st->index)
		columns -= input_char_width(st, st->view_start++);

	/* Do not start a viewport with a detached combining character. */
	while (st->view_start < st->index && !input_char_width(st, st->view_start))
		st->view_start++;

	st->cursor_x = (int) MIN(columns, MAX(0, w->w - 1));

	if (st->len > 0) {
		int drawn = 0;

		for (int i = st->view_start; i < st->len; i++) {
			int width = input_char_width(st, i);
			if (drawn + width > w->w)
				break;
			w_addch(w->win, (st->force_chr ?: st->text[i]));
			drawn += width;
		}

	} else if (st->placeholder) {
		waddwstr(w->win, st->placeholder);
	}

	wmove(w->win, st->cursor_y, st->cursor_x);
}

void input_free(struct widget *w)
{
	if (!w)
		return;

	struct widget_input *st = w->state;

	if (st) {
		free(st->placeholder);
		free(st->text);
		free(st);
	}
}

bool __input_unchr(struct widget_input *state)
{
	int index = state->index;

	if (index > 0) {
		if (index < state->len)
			wmemmove(&state->text[index - 1],
				 &state->text[index],
				 (size_t) (state->len - index + 1));
		state->len--;
		state->text[state->len] = L'\0';
		return true;
	}
	return false;
}

bool __input_append(struct widget_input *state, wchar_t c)
{
	if (state->len >= state->max_length)
		return false;
	if (state->cap <= (state->len + 1)) {
		size_t cap = (size_t) state->cap + 1;
		wchar_t *text = realloc(state->text, cap * sizeof(wchar_t));

		if (!text) {
			warn("realloc");
			return false;
		}
		state->text = text;
		state->cap = (int) cap;
	}

	int index = state->index;

	if (index < state->len)
		wmemmove(&state->text[index + 1],
			 &state->text[index],
			 (size_t) (state->len - index + 1));

	state->text[index] = c;
	state->len++;
	state->text[state->len] = L'\0';

	return true;
}

static void dec_cursor(const struct widget *w)
{
	struct widget_input *st = w->state;

	st->index--;
	st->index = MAX(0, st->index);

	st->cursor_x = (st->index > w->w) ? w->w : st->index;
}

static void inc_cursor(const struct widget *w)
{
	struct widget_input *st = w->state;

	st->index++;
	st->index = MIN(st->index, st->len);

	st->cursor_x = (st->index > w->w) ? w->w : st->index;
}

int input_input(const struct widget *w, wchar_t key)
{
	struct widget_input *st = w->state;

	if (st->finished || !widget_is_interactive(w))
		return 0;

	switch (key) {
		case KEY_ENTER:
		case L'\n':
			if (st->finish_on_enter)
				st->finished = true;
			break;
		case KEY_LEFT:
			dec_cursor(w);
			break;
		case KEY_RIGHT:
			inc_cursor(w);
			break;
		case KEY_HOME:
			st->index = 0;
			break;
		case KEY_END:
			st->index = st->len;
			break;
		case KEY_DC:
			if (st->index == st->len)
				return 0;

			wmemmove(st->text + st->index, st->text + st->index + 1,
				 (size_t) (st->len - st->index));

			st->len--;
			break;
		case KEY_BACKSPACE:
		case L'\b':
		case 127:
			if (!__input_unchr(st))
				return 0;
			dec_cursor(w);
			break;
		default:
			if (!__input_append(st, key))
				return 0;
			inc_cursor(w);
			break;
	}

	return 1;
}

static int input_event(const struct widget *w, wchar_t key, bool keycode)
{
	if (!widget_is_interactive(w))
		return 0;
	if (keycode) {
		switch (key) {
			case KEY_ENTER:
			case KEY_LEFT:
			case KEY_RIGHT:
			case KEY_BACKSPACE:
			case KEY_HOME:
			case KEY_END:
			case KEY_DC:
				return input_input(w, key);
			default:
				return 0;
		}
	}
	if (key == L'\n' || key == L'\b' || key == 127)
		return input_input(w, key);
	if (!iswprint((wint_t) key))
		return 0;
	struct widget_input *st = w->state;
	if (st->finished)
		return 0;
	if (st->len >= st->max_length)
		return -1;
	if (!__input_append(st, key))
		return 0;
	inc_cursor(w);
	return 1;
}

bool input_getter(struct widget *w, enum widget_property prop, void *value)
{
	struct widget_input *st = w->state;

	if (prop == PROP_INPUT_STATE) {
		*(bool *) value = st->finished;
		return true;

	} else if (prop == PROP_INPUT_VALUE) {
		*(wchar_t **) value = st->text;
		return true;

	} else if (prop == PROP_INPUT_MAX_LENGTH) {
		*(int *) value = st->max_length;
		return true;

	} else if (prop == PROP_INPUT_FINISH_ON_ENTER) {
		*(bool *) value = st->finish_on_enter;
		return true;

	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

bool input_setter(struct widget *w, enum widget_property prop, const void *value)
{
	struct widget_input *st = w->state;

	if (prop == PROP_INPUT_STATE) {
		st->finished = !!(*(const bool *) value);
		return true;

	} else if (prop == PROP_INPUT_VALUE) {
		const wchar_t *in = value;
		size_t length = wcslen(in ?: L"");
		if (length > (size_t) st->max_length)
			return false;
		wchar_t *text = wcsdup(in ?: L"");
		if (!text) {
			warn("wcsdup");
			return false;
		}

		free(st->text);
		st->text = text;
		st->len = (int) length;
		st->cap = st->len + 1;
		st->index = st->len;
		st->view_start = 0;
		st->cursor_x = (w->w > 0) ? MIN(st->index, w->w) : st->index;
		st->cursor_y = 0;
		return true;

	} else if (prop == PROP_INPUT_MAX_LENGTH) {
		int limit = *(const int *) value;
		if (limit < st->len || limit > INT_MAX - 1)
			return false;
		st->max_length = limit;
		return true;

	} else if (prop == PROP_INPUT_FINISH_ON_ENTER) {
		st->finish_on_enter = *(const bool *) value;
		return true;

	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

static const struct widget_ops input_ops = {
	.measure          = input_measure,
	.layout           = NULL,
	.render           = input_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = input_free,
	.input            = input_input,
	.input_event      = input_event,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = input_setter,
	.getter           = input_getter,
	.getter_index     = NULL,
};

struct widget *make_input(const wchar_t *initdata, const wchar_t *placeholder)
{
	struct widget *w = widget_create(WIDGET_INPUT);
	if (!w)
		return NULL;

	struct widget_input *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_input: calloc");
		widget_free(w);
		return NULL;
	}

	state->text = wcsdup(initdata ?: L"");

	if (!state->text) {
		free(state);
		widget_free(w);
		return NULL;
	}

	size_t length = wcslen(state->text);

	if (length > INT_MAX - 1) {
		free(state->text);
		free(state);
		widget_free(w);
		return NULL;
	}

	state->len = (int) length;
	state->max_length = INT_MAX - 1;
	state->finish_on_enter = true;
	state->cap = state->len + 1;
	state->index = state->len;
	state->cursor_x = state->len;

	if (placeholder) {
		state->placeholder = wcsdup(placeholder);
		if (!state->placeholder) {
			free(state->text);
			free(state);
			widget_free(w);
			return NULL;
		}
	}

	w->state      = state;
	w->ops        = &input_ops;
	w->color_pair = COLOR_PAIR_BUTTON;
	w->attrs      = ATTR_CAN_FOCUS | ATTR_CAN_CURSOR;

	/* INPUT is normally stretched horizontally, but height stays fixed */
	w->flex_w = 1;        /* expand width */
	w->flex_h = 0;        /* no flex vertically */
	w->stretch_w = true;  /* full width */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 0;
	w->shrink_h = 0;

	return w;
}

struct widget *make_input_password(const wchar_t *initdata, const wchar_t *placeholder)
{
	struct widget *w = make_input(initdata, placeholder);
	if (w) {
		struct widget_input *st = w->state;
		st->force_chr = L'*';
	}
	return w;
}
