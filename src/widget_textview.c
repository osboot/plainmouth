// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "warray.h"
#include "widget.h"

struct widget_textview {
	struct text_viewport text;
	int vscroll_pos;
};

void viewport_create(struct text_viewport *vp, const wchar_t *text)
{
	const wchar_t *s = text;
	const wchar_t *e = text + wcslen(text);

	int capacity = 128;

	vp->ncols = 0;
	vp->nlines = 0;
	vp->lines = calloc((size_t) capacity, sizeof(wchar_t *));

	if (!vp->lines) {
		warn("calloc failed");
		return;
	}

	while (s < e) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;
		size_t len = (size_t) (c - s);
		wchar_t *line = wcsndup(s, len);

		if (!line) {
			warnx("unable to duplicate string");
			viewport_free(vp);
			return;
		}

		if (vp->nlines == capacity) {
			wchar_t **newlines;

			capacity *= 2;
			newlines = realloc(vp->lines, sizeof(wchar_t *) * (size_t) capacity);

			if (!newlines) {
				warn("realloc failed");
				viewport_free(vp);
				return;
			}
			vp->lines = newlines;
		}

		vp->lines[vp->nlines++] = line;
		vp->ncols = MAX(vp->ncols, (int) len);

		s = (c == e) ? e : c + 1;
	}
}

void viewport_free(struct text_viewport *vp)
{
	for (int i = 0; i < vp->nlines; i++)
		free(vp->lines[i]);
	free(vp->lines);
}

void viewport_draw(WINDOW *win, struct text_viewport *vp, int scroll_pos)
{
	int nlines, ncols, y = 0;
	getmaxyx(win, nlines, ncols);
	werase(win);

	for (int i = scroll_pos; i < vp->nlines && y < nlines; i++)
		mvwaddnwstr(win, y++, 0, vp->lines[i], ncols);

	wnoutrefresh(win);
}

static void draw_vscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_height)
{
	int view_width, view_height;
	getmaxyx(scrollwin, view_height, view_width);

	if ((content_height - view_height) <= 0)
		return;

	int thumb_size = MAX(1, (view_height * view_height) / content_height);
	int thumb_pos = (scroll_pos * (view_height - thumb_size)) / (content_height - view_height);

	wattron(scrollwin, COLOR_PAIR(color) | A_NORMAL);
	for (int i = 0; i < view_height; i++)
		mvwaddch(scrollwin, i, view_width - 1, ACS_CKBOARD);
	wattroff(scrollwin, COLOR_PAIR(color) | A_NORMAL);

	wattron(scrollwin, COLOR_PAIR(color) | A_REVERSE);
	for (int i = 0; i < thumb_size; i++)
		mvwaddch(scrollwin, thumb_pos + i, view_width - 1, ACS_VLINE);
	wattroff(scrollwin, COLOR_PAIR(color) | A_REVERSE);
}

static void textview_measure(struct widget *w)
{
	struct widget_textview *state = w->state.textview;

	w->min_w = state->text.ncols;
	w->min_h = state->text.nlines;

	if (w->min_h > 5)
		w->min_h = 5;
}

static void textview_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	struct widget_textview *state = w->state.textview;

	viewport_draw(w->win, &state->text, state->vscroll_pos);
	draw_vscroll(w->win, color, state->vscroll_pos, state->text.nlines);

	wmove(w->win, 0, 0);

	if (getmaxy(w->win) < state->text.nlines) {
		w->attrs |= ATTR_CAN_FOCUS;
	}
}

static void textview_free(struct widget *w)
{
	if (w->state.textview) {
		viewport_free(&w->state.textview->text);
		free(w->state.textview);
	}
}

static int textview_input(const struct widget *w, wchar_t key)
{
	struct widget_textview *state = w->state.textview;

	if ((state->text.nlines - w->h) <= 0)
		return 0;

	switch (key) {
		case KEY_UP:
			if (state->vscroll_pos > 0)
				state->vscroll_pos--;
			break;

		case KEY_DOWN:
			if (state->vscroll_pos < state->text.nlines - w->h)
				state->vscroll_pos++;
			break;

		case KEY_PPAGE:
			state->vscroll_pos = MAX(0, state->vscroll_pos - w->h);
			break;

		case KEY_NPAGE:
			state->vscroll_pos = MIN(state->text.nlines - w->h, state->vscroll_pos + w->h);
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

	viewport_create(&state->text, text);

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
