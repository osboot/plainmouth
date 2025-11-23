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

	while (s <= e) {
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

WINDOW *window_new(WINDOW *parent,
		int nlines, int ncols, int begin_y, int begin_x,
		const char *what)
{
	WINDOW *win;

	if (parent)
		win = derwin(parent, nlines, ncols, begin_y, begin_x);
	else
		win = newwin(nlines, ncols, begin_y, begin_x);

	if (!win)
		warnx("unable to create %s window (%dx%d)", what, nlines, ncols);
	else if (IS_DEBUG())
		warnx("%s (%p) window (%dx%d) was created", what, win, nlines, ncols);

	return win;
}

void window_free(WINDOW *win, const char *what)
{
	if (win) {
		if (delwin(win) == ERR)
			warnx("unable to free %s (%p) window", what, win);
		else if (IS_DEBUG())
			warnx("%s (%p) window was freed up", what, win);
	}
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

	nlines = MIN(nlines, LINES);
	ncols  = MIN(ncols,  COLS);

	position_center(ncols, nlines, &begin_y, &begin_x);

	w->_main = window_new(NULL, nlines, ncols, begin_y, begin_x, "mainwin");
	if (!w->_main)
		return false;
	wbkgd(w->_main, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (borders) {
		wborder(w->_main,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);

		w->win = window_new(w->_main, nlines - 2, ncols - 2, 1, 1, "work aria");
		if (!w->win) {
			window_free(w->_main, "mainwin");
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
		window_free(w->win, "mainwin work area");
}

PANEL *mainwin_panel_new(struct mainwin *w, const void *data)
{
	PANEL *panel = new_panel(w->_main);

	if (!panel) {
		warnx("unable to create mainwin panel");
		return NULL;
	}
	if (IS_DEBUG())
		warnx("mainwin panel (%p) was created", panel);

	if (data)
		set_panel_userptr(panel, data);

	return panel;
}

void mainwin_panel_free(PANEL *panel)
{
	if (!panel)
		return;

	void *data = (void *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	if (del_panel(panel) == ERR)
		warnx("unable to free mainwin (%p) panel", panel);
	else if (IS_DEBUG())
		warnx("mainwin (%p) panel was freed up", panel);

	window_free(win, "mainwin");
	free(data);
}
