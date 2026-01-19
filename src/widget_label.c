// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "warray.h"
#include "widget.h"

struct widget_label {
	struct warray lines;
	int ncols;
};

static void label_init_lines(struct widget_label *st, const wchar_t *text)
{
	warray_init(&st->lines);
	st->ncols = 0;

	const wchar_t *s = text;
	while (*s) {
		const wchar_t *e = wcschr(s, L'\n');
		if (!e)
			e = s + wcslen(s);

		size_t len = (size_t) (e - s);
		warray_push(&st->lines, s, len);

		int line_width = 0;
		for (size_t i = 0; i < len; i++) {
			int w = wcwidth(s[i]);
			if (w > 0)
				line_width += w;
		}
		st->ncols = MAX(st->ncols, line_width);

		s = (*e == L'\n') ? e + 1 : e;
	}

	if (*text && text[wcslen(text) - 1] == L'\n')
		warray_push(&st->lines, L"", 0);
}

static void label_measure(struct widget *w)
{
	struct widget_label *state = w->state.label;

	w->min_h = (int) state->lines.size;
	w->min_w = state->ncols;
}

static void label_render(struct widget *w)
{
	struct widget_label *state = w->state.label;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	int maxy = getmaxy(w->win);
	int maxx = getmaxx(w->win);

	werase(w->win);

	for (int y = 0; y < maxy && (size_t) y < state->lines.size; y++) {
		const wchar_t *line = warray_get(&state->lines, (size_t) y);
		if (line)
			mvwaddnwstr(w->win, y, 0, line, maxx);
	}
}

static void label_free(struct widget *w)
{
	struct widget_label *state = w->state.label;
	if (state) {
		warray_free(&state->lines);
		free(state);
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

	label_init_lines(state, line);

	w->state.label = state;
	w->color_pair  = COLOR_PAIR_WINDOW;
	w->measure     = label_measure;
	w->render      = label_render;
	w->free_data   = label_free;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false; /* do not expand horizontally */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
