// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>
#include <panel.h>

#include "helpers.h"
#include "widget.h"

void focus_init(struct focuses *focuses, bool (*on_change)(void *data, bool in_focus))
{
	TAILQ_INIT(&focuses->head);
	focuses->on_change = on_change;
}

bool focus_new(struct focuses *focuses, void *data)
{
	struct focus *new = calloc(1, sizeof(*new));

	if (!new) {
		warnx("focus_new: no memory");
		return false;
	}

	new->data = data;
	TAILQ_INSERT_TAIL(&focuses->head, new, entries);

	if (focuses->on_change) {
		struct focus *curr = focus_current(focuses);
		focuses->on_change(data, (curr->data == data));
	}

	return true;
}

void focus_free(struct focuses *focuses)
{
	struct focus *f1, *f2;

	f1 = TAILQ_FIRST(&focuses->head);
	while (f1) {
		f2 = TAILQ_NEXT(f1, entries);
		free(f1);
		f1 = f2;
	}
}

struct focus *focus_current(struct focuses *focuses)
{
	return TAILQ_FIRST(&focuses->head);
}

void focus_set(struct focuses *focuses, void *data)
{
	struct focus *curr = focus_current(focuses);

	if (!curr)
		return;

	if (curr->data != data) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, curr, entries);
		TAILQ_INSERT_HEAD(&focuses->head, curr, entries);

		if (focuses->on_change) {
			curr = focus_current(focuses);
			focuses->on_change(curr->data, true);
		}
	}
}

void focus_next(struct focuses *focuses)
{
	struct focus *curr = focus_current(focuses);

	if (curr) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, curr, entries);
		TAILQ_INSERT_TAIL(&focuses->head, curr, entries);

		if (focuses->on_change) {
			curr = focus_current(focuses);
			focuses->on_change(curr->data, true);
		}
	}
}

void focus_prev(struct focuses *focuses)
{
	struct focus *prev = TAILQ_LAST(&focuses->head, focushead);
	struct focus *curr = focus_current(focuses);

	if (prev && prev != curr) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, prev, entries);
		TAILQ_INSERT_HEAD(&focuses->head, prev, entries);

		if (focuses->on_change)
			focuses->on_change(prev->data, true);
	}
}

int simple_round(float number)
{
	// Example: 15.4 + 0.5 = 15.9 -> 15
	//          15.6 + 0.5 = 16.1 -> 16
	return (int) (number >= 0 ? number + 0.5 : number - 0.5);
}

void position_center(int width, int height, int *begin_y, int *begin_x)
{
	float center_y = (float) LINES / 2;
	float center_x = (float) COLS  / 2;
	float half_w = (float) width   / 2;
	float half_h = (float) height  / 2;

	if (begin_y && *begin_y < 0)
		*begin_y = simple_round(center_y - half_h);

	if (begin_x && *begin_x < 0)
		*begin_x = simple_round(center_x - half_w);
}

bool get_abs_cursor(WINDOW *target, WINDOW *win, int *cursor_y, int *cursor_x)
{
	if (!target || !win || !cursor_y || !cursor_x)
		return false;

	WINDOW *cur = win;

	int y, x;
	getyx(cur, y, x);

	while (cur != target) {
		WINDOW *parent = wgetparent(cur);

		if (!parent)
			return false;

		int py, px;
		getparyx(cur, py, px);

		if (py == -1 && px == -1)
			return false;

		y += py;
		x += px;

		cur = parent;
	}

	*cursor_y = y;
	*cursor_x = x;

	return true;
}

void text_size(const wchar_t *text, int *lines, int *columns)
{
	ssize_t nlines, ncols;
	const wchar_t *s, *e;

	nlines = ncols = 0;

	if (!text || *text == '\0')
		goto empty;

	s = text;
	e = s + wcslen(s);

	while (s < e) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;

		ncols = MAX(ncols, (c - s));
		nlines += 1;

		s = c + 1;
	}

	if (nlines < 0) nlines = 0;
	if (ncols  < 0)  ncols = 0;

empty:
	if (lines)   *lines   = (int) nlines;
	if (columns) *columns = (int) ncols;
}

void write_mvwtext(WINDOW *win, int y, int x, const wchar_t *text)
{
	const wchar_t *s, *e;

	if (!text)
		return;

	s = text;
	e = s + wcslen(s);

	while (s < e) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;
		mvwaddnwstr(win, y++, x, s, (int) (c - s));
		s = c + 1;
	}
}

bool widget_borders(struct request *req, chtype bdr[BORDER_SIZE])
{
	struct borders borders[BORDER_SIZE] = {
		[BORDER_LS] = { "border_ls", ACS_VLINE    },
		[BORDER_RS] = { "border_rs", ACS_VLINE    },
		[BORDER_TS] = { "border_ts", ACS_HLINE    },
		[BORDER_BS] = { "border_bs", ACS_HLINE    },
		[BORDER_TL] = { "border_tl", ACS_ULCORNER },
		[BORDER_TR] = { "border_tr", ACS_URCORNER },
		[BORDER_BL] = { "border_bl", ACS_LLCORNER },
		[BORDER_BR] = { "border_br", ACS_LRCORNER },
	};
	bool res = false;

	if (!req_get_bool(req, "border", true))
		return res;

	for (int i = 0; i < BORDER_SIZE; i++) {
		if (req_get_bool(req, borders[i].name, true)) {
			res = true;
			bdr[i] = borders[i].chr;
		} else {
			bdr[i] = ' ';
		}
	}

	return res;
}

bool mainwin_new(struct request *req, struct mainwin *w, int def_nlines, int def_ncols)
{
	chtype bdr[BORDER_SIZE];

	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int nlines  = req_get_int(req, "height", -1);
	int ncols   = req_get_int(req, "width",  -1);
	bool borders = widget_borders(req, bdr);

	if (nlines < 0) {
		nlines = def_nlines;
		if (borders)
			nlines += 2;
	}

	if (ncols < 0) {
		ncols = def_ncols;
		if (borders)
			ncols += 2;
	}

	position_center(ncols, nlines, &begin_y, &begin_x);

	w->_main = newwin(nlines, ncols, begin_y, begin_x);
	if (!w->_main) {
		warnx("unable to create new ncurses window");
		return false;
	}
	wbkgd(w->_main, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (borders) {
		wborder(w->_main,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);

		w->win = derwin(w->_main, nlines - 2, ncols - 2, 1, 1);
		if (!w->win) {
			warnx("unable to create window area");
			delwin(w->_main);
			return false;
		}
	} else {
		w->win = w->_main;
	}
	return true;
}

void mainwin_free(struct mainwin *w)
{
	if (w->win && w->win != w->_main)
		delwin(w->win);
	if (w->_main)
		delwin(w->_main);
}

PANEL *mainwin_panel(struct mainwin *w)
{
	return new_panel(w->_main);
}
