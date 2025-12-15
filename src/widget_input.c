// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

static void input_measure(struct widget *w)                                        __attribute__((nonnull(1)));
static void input_render(struct widget *w)                                         __attribute__((nonnull(1)));
static int input_input(const struct widget *w, wchar_t key)                        __attribute__((nonnull(1)));
static bool input_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1)));
static void input_free(struct widget *w);
static bool __input_unchr(struct widget_input *state)                              __attribute__((nonnull(1)));
static bool __input_append(struct widget_input *state, wchar_t c)                  __attribute__((nonnull(1)));


struct widget_input {
	wchar_t force_chr;

	wchar_t *placeholder;

	wchar_t *text;
	int cap;
	int len;

	bool finished;
};

void input_measure(struct widget *w)
{
	struct widget_input *state = w->state.input;

	/* Minimum workable size */
	w->min_h = 1;
	w->min_w = 1;

	/* Preferred size = size of placeholder or existing text */
	if (state->placeholder) {
		int len = (int) wcslen(state->placeholder);
		w->pref_w = MAX(w->pref_w, len);
	}

	w->pref_w = MAX(w->pref_w, state->len);
	w->pref_h = 1;

	/* Max size: unlimited unless user overrides via API later */
	w->max_w = INT_MAX;
	w->max_h = 1;
}

void input_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	wmove(w->win, 0, 0);
	wclrtoeol(w->win);

	struct widget_input *state = w->state.input;

	if (state->len > 0) {
		int width = MIN(state->len, w->w);
		int i;

		for (i = 0; i < width; i++)
			w_addch(w->win, (state->force_chr ?: state->text[i]));
		if (i == w->w)
			wmove(w->win, 0, w->w - 1);


	} else if (state->placeholder) {
		waddwstr(w->win, state->placeholder);
	}

	wnoutrefresh(w->win);
}

void input_free(struct widget *w)
{
	if (w->state.input) {
		free(w->state.input->placeholder);
		free(w->state.input->text);
		free(w->state.input);
	}
}

bool __input_unchr(struct widget_input *state)
{
	if (state->len > 0)
		state->text[--state->len] = '\0';
	return true;
}

bool __input_append(struct widget_input *state, wchar_t c)
{
	int len = state->len + 2;

	if (state->cap < len) {
		wchar_t *text = realloc(state->text, (size_t) len * sizeof(wchar_t));

		if (!text) {
			warn("realloc");
			return false;
		}
		state->text = text;
		state->cap = len;
	}

	state->text[state->len++] = c;
	state->text[state->len] = L'\0';

	return true;
}

int input_input(const struct widget *w, wchar_t key)
{
	struct widget_input *state = w->state.input;

	if (state->finished)
		return 0;

	switch (key) {
		case KEY_ENTER:
		case L'\n':
			state->finished = true;
			break;
		case KEY_BACKSPACE:
		case L'\b':
		case 127:
			if (!__input_unchr(state))
				return 0;
			break;
		default:
			if(!__input_append(state, key))
				return 0;
	}

	return 1;
}

bool input_getter(struct widget *w, enum widget_property prop, void *value)
{
	if (prop == PROP_INPUT_STATE) {
		bool *finished = value;
		*finished = w->state.input->finished;
		return true;

	} else if (prop == PROP_INPUT_VALUE) {
		wchar_t **text = value;
		*text = w->state.input->text;
		return true;

	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

struct widget *make_input(const wchar_t *initdata, const wchar_t *placeholder)
{
	struct widget *w = widget_create(WIDGET_INPUT);
	if (!w)
		return NULL;

	struct widget_input *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_button: calloc");
		widget_free(w);
		return NULL;
	}

	if (initdata) {
		state->text = wcsdup(initdata);
		state->len = wcslen(initdata);
		state->cap = state->len;
	}

	if (placeholder)
		state->placeholder = wcsdup(placeholder);

	w->state.input = state;
	w->measure     = input_measure;
	w->render      = input_render;
	w->free_data   = input_free;
	w->input       = input_input;
	w->getter      = input_getter;
	w->color_pair  = COLOR_PAIR_BUTTON;
	w->attrs       = ATTR_CAN_FOCUS | ATTR_CAN_CURSOR;

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
		w->state.input->force_chr = L'*';
	}
	return w;
}
