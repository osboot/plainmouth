// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

static void checkbox_measure(struct widget *w) __attribute__((nonnull(1)));
static void checkbox_render(struct widget *w) __attribute__((nonnull(1)));
static int checkbox_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool checkbox_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1)));
static bool checkbox_setter(struct widget *w, enum widget_property prop, const void *value) __attribute__((nonnull(1)));
static void checkbox_free(struct widget *w);

struct widget_checkbox {
	bool checked;
	bool multisel;
};

void checkbox_measure(struct widget *w)
{
	// "[x]"
	w->min_h = w->pref_h = 1;
	w->min_w = w->pref_w = 3;
}

void checkbox_render(struct widget *w)
{
	struct widget_checkbox *state = w->state.checkbox;
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	wbkgd(w->win, COLOR_PAIR(color));

	if (state->multisel)
		mvwaddstr(w->win, 0, 0, (state->checked ? "[x]" : "[ ]"));
	else
		mvwaddstr(w->win, 0, 0, (state->checked ? "(x)" : "( )"));
}

void checkbox_free(struct widget *w)
{
	if (w->state.checkbox) {
		free(w->state.checkbox);
	}
}

int checkbox_input(const struct widget *w, wchar_t key)
{
	struct widget_checkbox *state = w->state.checkbox;

	switch (key) {
		case L' ':
			state->checked = !state->checked;
			break;
		default:
			return 0;
	}
	return 1;
}

bool checkbox_getter(struct widget *w, enum widget_property prop, void *value)
{
	if (prop == PROP_CHECKBOX_STATE) {
		bool *finished = value;
		*finished = w->state.checkbox->checked;
		return true;
	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

bool checkbox_setter(struct widget *w, enum widget_property prop, const void *value)
{
	if (prop == PROP_CHECKBOX_STATE) {
		w->state.checkbox->checked = !!(*(const bool *) value);
		return true;
	} else {
		errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

struct widget *make_checkbox(bool checked, bool multisel)
{
	struct widget *w = widget_create(WIDGET_CHECKBOX);
	if (!w)
		return NULL;

	struct widget_checkbox *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_checkbox: calloc");
		widget_free(w);
		return NULL;
	}

	state->checked  = checked;
	state->multisel = multisel;

	w->state.checkbox = state;

	w->measure    = checkbox_measure;
	w->render     = checkbox_render;
	w->free_data  = checkbox_free;
	w->input      = checkbox_input;
	w->getter     = checkbox_getter;
	w->setter     = checkbox_setter;
	w->color_pair = COLOR_PAIR_BUTTON;
	w->attrs      = ATTR_CAN_FOCUS;

	return w;
}
