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

static void label_init_lines(struct widget_label *st, const wchar_t *text) __attribute__((nonnull(1)));
static void label_measure(struct widget *w) __attribute__((nonnull(1)));
static void label_render(struct widget *w) __attribute__((nonnull(1)));
static void label_free(struct widget *w);


void label_init_lines(struct widget_label *st, const wchar_t *text)
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

void label_measure(struct widget *w)
{
	struct widget_label *st = w->state;

	w->min_h = (int) st->lines.size;
	w->min_w = st->ncols;
}

void label_render(struct widget *w)
{
	struct widget_label *st = w->state;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	int maxy = getmaxy(w->win);
	int maxx = getmaxx(w->win);

	werase(w->win);

	for (int y = 0; y < maxy && (size_t) y < st->lines.size; y++) {
		const wchar_t *line = warray_get(&st->lines, (size_t) y);
		if (line)
			mvwaddnwstr(w->win, y, 0, line, maxx);
	}
}

void label_free(struct widget *w)
{
	if (!w)
		return;

	struct widget_label *st = w->state;

	if (st) {
		warray_free(&st->lines);
		free(st);
	}
}

static const struct widget_ops label_ops = {
	.measure          = label_measure,
	.layout           = NULL,
	.render           = label_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = label_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = NULL,
	.getter           = NULL,
	.getter_index     = NULL,
};

struct widget *make_label(const wchar_t *line)
{
	struct widget *w = widget_create(WIDGET_LABEL);
	if (!w)
		return NULL;

	struct widget_label *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_label: calloc");
		widget_free(w);
		return NULL;
	}

	label_init_lines(state, line);

	w->state      = state;
	w->ops        = &label_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false; /* do not expand horizontally */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
