// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "warray.h"
#include "widget.h"

struct widget_textview {
	struct warray text;
	int vscroll_pos;
	int nlines;
	int ncols;
};

static void viewport_create(struct widget_textview *wt, const wchar_t *text)                           __attribute__((nonnull(1,2)));
static void viewport_draw(WINDOW *win, struct widget_textview *wt, int scroll_pos)                     __attribute__((nonnull(1,2)));
static void textview_measure(struct widget *w)                                                         __attribute__((nonnull(1)));
static void textview_render(struct widget *w)                                                          __attribute__((nonnull(1)));
static void textview_free(struct widget *w)                                                            __attribute__((nonnull(1)));
static int textview_input(const struct widget *w, wchar_t key)                                         __attribute__((nonnull(1)));


void viewport_create(struct widget_textview *wt, const wchar_t *text)
{
	const wchar_t *s = text;
	const wchar_t *e = text + wcslen(text);

	wt->ncols = 0;
	wt->nlines = 0;

	warray_init(&wt->text);

	while (s < e) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;
		size_t len = (size_t) (c - s);

		if (warray_push(&wt->text, s, len) < 0) {
			warnx("unable to append string");
			warray_free(&wt->text);
			return;
		}

		wt->ncols = MAX(wt->ncols, (int) len);
		wt->nlines++;

		s = (c == e) ? e : c + 1;
	}
}

void viewport_draw(WINDOW *win, struct widget_textview *wt, int scroll_pos)
{
	int nlines, ncols, y = 0;
	getmaxyx(win, nlines, ncols);
	werase(win);

	for (int i = scroll_pos; i < wt->nlines && y < nlines; i++) {
		const wchar_t *line = warray_get(&wt->text, (size_t) i);
		if (line)
			mvwaddnwstr(win, y++, 0, line, ncols);
	}

	wnoutrefresh(win);
}

void textview_measure(struct widget *w)
{
	struct widget_textview *state = w->state.textview;

	w->pref_w = state->ncols + 1; // text width + optional scrollbar
	w->pref_h = state->nlines;

	w->min_w = state->ncols;
	w->min_h = 1;
}

void textview_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	struct widget_textview *state = w->state.textview;

	viewport_draw(w->win, state, state->vscroll_pos);
	widget_draw_vscroll(w->win, color, state->vscroll_pos, state->nlines);

	wmove(w->win, 0, 0);

	if (getmaxy(w->win) < state->nlines) {
		w->attrs |= ATTR_CAN_FOCUS;
	}
}

void textview_free(struct widget *w)
{
	if (w->state.textview) {
		warray_free(&w->state.textview->text);
		free(w->state.textview);
	}
}

int textview_input(const struct widget *w, wchar_t key)
{
	struct widget_textview *state = w->state.textview;

	if ((state->nlines - w->h) <= 0)
		return 0;

	switch (key) {
		case KEY_UP:
			if (state->vscroll_pos > 0)
				state->vscroll_pos--;
			break;

		case KEY_DOWN:
			if (state->vscroll_pos < state->nlines - w->h)
				state->vscroll_pos++;
			break;

		case KEY_PPAGE:
			state->vscroll_pos = MAX(0, state->vscroll_pos - w->h);
			break;

		case KEY_NPAGE:
			state->vscroll_pos = MIN(state->nlines - w->h, state->vscroll_pos + w->h);
			break;

		default:
			return 0;
	}

	return 1;
}

struct widget *make_textview(const wchar_t *text)
{
	struct widget_textview *state = NULL;
	struct widget *w = widget_create(WIDGET_LABEL);

	if (!w)
		return NULL;

	state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_label: calloc");
		widget_free(w);
		return NULL;
	}

	viewport_create(state, text);

	w->state.textview = state;
	w->color_pair     = COLOR_PAIR_WINDOW;
	w->measure        = textview_measure;
	w->render         = textview_render;
	w->input          = textview_input;
	w->free_data      = textview_free;

	w->flex_w = 1;
	w->flex_h = 1;

	/* Textview expands in both dimensions */
	w->flex_w = 1;
	w->flex_h = 1;
	w->stretch_w = true;
	w->stretch_h = true;

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
