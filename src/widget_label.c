// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

struct widget_label {
	wchar_t *text;
};

static void label_measure(struct widget *w)
{
	w->min_h = 1;
	w->min_w = (int) wcslen(w->state.label->text);
}

static void label_render(struct widget *w)
{
	mvwprintw(w->win, 0, 0, "%ls", w->state.label->text);
}

static void label_free(struct widget *w)
{
	if (w->state.label) {
		free(w->state.label->text);
		free(w->state.label);
	}
}

struct widget *make_label(const wchar_t *line)
{
	struct widget_label *state = NULL;
	struct widget *w = widget_create(WIDGET_LABEL);

	if (!w)
		return NULL;

	state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_label: calloc");
		widget_free(w);
		return NULL;
	}

	state->text = wcsdup(line ?: L"");

	w->state.label = state;
	w->color_pair  = COLOR_PAIR_WINDOW;
	w->measure     = label_measure;
	w->render      = label_render;
	w->free_data   = label_free;

	return w;
}
