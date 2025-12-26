// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

static void button_measure(struct widget *w)                                        __attribute__((nonnull(1)));
static void button_render(struct widget *w)                                         __attribute__((nonnull(1)));
static int button_input(const struct widget *w, wchar_t key)                        __attribute__((nonnull(1)));
static bool button_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1)));
static void button_free(struct widget *w);

struct widget_button {
	wchar_t *text;
	bool pressed;
};

void button_measure(struct widget *w)
{
	w->min_h = 1;
	w->min_w = (int) wcslen(w->state.button->text) + 2; /* "[OK]" style */
}

void button_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	wbkgd(w->win, COLOR_PAIR(color));
	w_mvprintw(w->win, 0, 0, L"[%ls]", w->state.button->text);
}

void button_free(struct widget *w)
{
	if (w->state.button) {
		free(w->state.button->text);
		free(w->state.button);
	}
}

int button_input(const struct widget *w, wchar_t key)
{
	if (key == L'\n' || key == KEY_ENTER) {
		w->state.button->pressed = !w->state.button->pressed;
		return 1;
	}

	return 0;
}

bool button_getter(struct widget *w, enum widget_property prop, void *value)
{
	if (prop == PROP_BUTTON_STATE) {
		bool *clicked = value;
		*clicked = w->state.button->pressed;
		return true;
	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

struct widget *make_button(const wchar_t *text)
{
	struct widget *w = widget_create(WIDGET_BUTTON);
	if (!w)
		return NULL;

	struct widget_button *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_button: calloc");
		widget_free(w);
		return NULL;
	}

	state->text = wcsdup(text ?: L"");
	state->pressed = false;

	w->state.button = state;
	w->measure      = button_measure;
	w->render       = button_render;
	w->input        = button_input;
	w->getter       = button_getter;
	w->free_data    = button_free;
	w->color_pair   = COLOR_PAIR_BUTTON;
	w->attrs        = ATTR_CAN_FOCUS;

	/* Buttons do not stretch by default — natural size only */
	w->flex_h = 0;
	w->flex_w = 0;
	w->stretch_h = false; /* stay 1 line tall */
	w->stretch_w = false; /* do not expand horizontally */
	w->shrink_h = w->shrink_w = 1;

	return w;
}
