// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
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

	w->nlines = MIN(nlines, LINES);
	w->ncols  = MIN(ncols,  COLS);

	position_center(w->ncols, w->nlines, &begin_y, &begin_x);

	w->_main = window_new(NULL, w->nlines, w->ncols, begin_y, begin_x, "mainwin");
	if (!w->_main)
		return false;
	wbkgd(w->_main, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (borders) {
		wborder(w->_main,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);

		w->win = window_new(w->_main, w->nlines - 2, w->ncols - 2, 1, 1, "work aria");
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

/*
 * ############################################################################
 */

struct widget *widget_create(enum widget_type type)
{
	struct widget *w = calloc(1, sizeof(*w));
	if (!w) {
		warn("calloc failed");
		return NULL;
	}

	w->type = type;
	TAILQ_INIT(&w->children);

	w->visible = true;
	w->flex = 0;
	w->color_pair = 1;

	return w;
}

void widget_add(struct widget *parent, struct widget *child)
{
	child->parent = parent;
	TAILQ_INSERT_TAIL(&parent->children, child, siblings);
}

void widget_free(struct widget *w)
{
	if (!w)
		return;

	while (!TAILQ_EMPTY(&w->children)) {
		struct widget *c = TAILQ_FIRST(&w->children);
		TAILQ_REMOVE(&w->children, c, siblings);
		widget_free(c);
	}

	if (w->panel) {
		del_panel(w->panel);
		w->panel = NULL;
	}

	if (w->win) {
		delwin(w->win);
		w->win = NULL;
	}

	switch (w->type) {
		case WIDGET_BUTTON:
			free(w->data.button.text);
			break;
		case WIDGET_LABEL:
			free(w->data.label.text);
			break;
		default:
			break;
	}
	free(w);
}

static const char *_widget_type[] = {
	[WIDGET_WINDOW] = "window",
	[WIDGET_LABEL]  = "label",
	[WIDGET_BUTTON] = "button",
	[WIDGET_INPUT]  = "input",
	[WIDGET_VBOX]   = "vbox",
	[WIDGET_HBOX]   = "hbox",
};

const char *widget_type(struct widget *w)
{
	return _widget_type[w->type];
}

/*
 * measure - Calculates the minimum size of each widget.
 *
 * Post-order traversal (children first).
 *
 * Examples:
 *   Label -> string width + padding, height 1.
 *   Button -> text width + 4, height 1.
 *   Input -> minimum width 10, height 1.
 *   Container (horizontal) -> adds up the widths of all children + indents, max height.
 *   Container (vertical) -> adds up the heights, max width.
 *
 * Result: Each widget has known min_w and min_h.
 */
void widget_measure(struct widget *w)
{
	if (!w)
		return;

	struct widget *c;

	TAILQ_FOREACH(c, &w->children, siblings)
		widget_measure(c);

	if (w->measure)
		w->measure(w);
}

/*
 * layout - Assigns final coordinates and dimensions to each widget.
 *
 * top-down, lx/ly are local coords inside parent.
 *
 * Result: Each widget knows: lx, ly, w, h.
 */
void widget_layout(struct widget *w, int lx, int ly, int width, int height)
{
	if (!w)
		return;

	if (lx >= 0) w->lx = lx;
	if (ly >= 0) w->ly = ly;

	if (width  >= 0) w->w = width;
	if (height >= 0) w->h = height;

	if (w->layout)
		w->layout(w);
}

static bool widget_maybe_recreate(struct widget *w)
{
	if (!w->win)
		return true;

	int cur_h, cur_w, cur_y, cur_x;

	getbegyx(w->win, cur_y, cur_x);
	getmaxyx(w->win, cur_h, cur_w);

	if (cur_h != w->h || cur_w != w->w || cur_y != w->ly || cur_x != w->lx) {
		delwin(w->win);
		w->win = NULL;
		return true;
	}

	return false;
}

static void widget_create_window(struct widget *w)
{
	if (w->parent == NULL) {
		/* root: absolute coords */
		w->win = newwin(w->h, w->w, w->ly, w->lx);
		if (!w->win) {
			warnx("widget_create_window: newwin failed for root");
			return;
		}
	} else {
		/* child: derived window */
		if (!w->parent->win)
			return;

		w->win = derwin(w->parent->win, w->h, w->w, w->ly, w->lx);
		if (!w->win) {
			w->visible = false;
			return;
		}
	}
	if (IS_DEBUG())
		warnx("%s (%p) window (%dx%d) was created with coords y=%d, x=%d",
			widget_type(w), w->win, w->h, w->w, w->ly, w->lx);

	if (w->color_pair)
		wbkgd(w->win, COLOR_PAIR(w->color_pair));

	w->visible = true;
}

/*
 * Recreate windows: create/del win/panel based on geometry
 */
void widget_recreate_windows(struct widget *w)
{
	if (!w)
		return;

	if (widget_maybe_recreate(w))
		widget_create_window(w);

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings)
		widget_recreate_windows(c);
}

/* render: clear+draw+wnoutrefresh, children after parent */
void widget_render(struct widget *w)
{
	if (!w || !w->visible)
		return;

	if (w->win) {
		werase(w->win);

		if (w->render)
			w->render(w);

		wnoutrefresh(w->win);
		w->dirty = false;
	}

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings)
		widget_render(c);
}

/* --------------------- Basic widgets implementations --------------------- */

/* Label */

static void label_measure(struct widget *w)
{
	w->min_h = 1;

	w->min_w = w->data.label.text
		? (int) strlen(w->data.label.text)
		: 0;

	if (w->min_w < 1)
		w->min_w = 1;
}

static void label_render(struct widget *w)
{
	mvwprintw(w->win, 0, 0, "%s", w->data.label.text ?: "");
}

/* Button */

static void button_measure(struct widget *w)
{
	w->min_h = 1;

	w->min_w = w->data.button.text
		? (int) strlen(w->data.button.text)
		: 0;

	w->min_w += 2; /* "[OK]" style */
}

static int button_on_key(struct widget *w, int key)
{
	if (key == '\n' || key == KEY_ENTER) {
		w->data.button.pressed = !w->data.button.pressed;
		w->dirty = true;
		return 1;
	}
	return 0;
}

static void button_render(struct widget *w)
{
	const char *txt = w->data.button.text ? w->data.button.text : "";
	int tw = (int) strlen(txt);
	int inner = tw + 2; /* brackets + text */
	int px = 0;

	if (w->w > inner)
		px = (w->w - inner) / 2;

	mvwprintw(w->win, 0, px, "[%s]", txt);

	if (w->data.button.pressed) {
		/* simple visual: invert */
		wattron(w->win, A_REVERSE);
		mvwprintw(w->win, 0, px, "[%s]", txt);
		wattroff(w->win, A_REVERSE);
	}
}

/* VBOX/HBOX: flex-aware layout */

static void vbox_measure(struct widget *w)
{
	struct widget *c;
	int sum_min_h = 0;
	int max_w = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		sum_min_h += c->min_h;
		max_w = MAX(max_w, c->min_w);
	}

	w->min_h = MAX(w->h, sum_min_h);
	w->min_w = MAX(w->w, max_w);
}

static void vbox_layout(struct widget *w)
{
	struct widget *c;

	/* distribute vertical space */
	int total_fixed = 0;
	int total_flex = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->flex > 0)
			total_flex += c->flex;
		else
			total_fixed += c->min_h;
	}

	int remaining = w->h - total_fixed;

	if (remaining < 0)
		remaining = 0;

	int y = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->flex > 0 && total_flex > 0)
			? (remaining * c->flex) / total_flex
			: c->min_h;

		if (ch < c->min_h)
			ch = c->min_h;

		if (y + ch > w->h)
			ch = MAX(0, w->h - y);

		widget_layout(c, 0, y, w->w, ch);
		y += ch;
	}
}

static void hbox_measure(struct widget *w)
{
	struct widget *c;

	int sum_min_w = 0;
	int max_h = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		sum_min_w += c->min_w;
		max_h = MAX(max_h, c->min_h);
	}

	w->min_w = sum_min_w;
	w->min_h = max_h;
}

static void hbox_layout(struct widget *w)
{
	struct widget *c;

	int total_fixed = 0, total_flex = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->flex > 0) total_flex += c->flex;
		else total_fixed += c->min_w;
	}

	int remaining = w->w - total_fixed;

	if (remaining < 0)
		remaining = 0;

	int x = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int cw = (c->flex > 0 && total_flex > 0)
			? (remaining * c->flex) / total_flex
			: c->min_w;

		if (cw < c->min_w)
			cw = c->min_w;

		if (x + cw > w->w)
			cw = MAX(0, w->w - x);

		widget_layout(c, x, 0, cw, w->h);
		x += cw;
	}
}

/* Window (container) measure/layout reuse vbox behavior (vertical stacking) */

static void window_measure(struct widget *w)
{
	vbox_measure(w);
	w->min_w += 2;
}

static void window_layout(struct widget *w)
{
	vbox_layout(w);
}

/* --------------------- Factory functions --------------------- */

struct widget *make_window(void)
{
	struct widget *w = widget_create(WIDGET_WINDOW);

	w->measure    = window_measure;
	w->layout     = window_layout;
	w->render     = NULL; /* could draw border */
	w->min_w      = 10;
	w->min_h      = 5;
	w->color_pair = COLOR_PAIR_MAIN;
	w->no_shrink  = true;

	return w;
}

struct widget *make_vbox(void)
{
	struct widget *w = widget_create(WIDGET_VBOX);

	w->measure = vbox_measure;
	w->layout  = vbox_layout;
	w->render  = NULL;
	w->color_pair = COLOR_PAIR_EXTRA1;

	return w;
}

struct widget *make_hbox(void)
{
	struct widget *w = widget_create(WIDGET_HBOX);

	w->measure = hbox_measure;
	w->layout  = hbox_layout;
	w->render  = NULL;
	w->color_pair = COLOR_PAIR_EXTRA2;

	return w;
}

struct widget *make_label(const char *text)
{
	struct widget *w = widget_create(WIDGET_LABEL);

	w->data.label.text = strdup(text);

	w->measure = label_measure;
	w->render  = label_render;

	return w;
}

struct widget *make_button(const char *text)
{
	struct widget *w = widget_create(WIDGET_BUTTON);

	w->data.button.text = strdup(text);
	w->data.button.pressed = false;

	w->measure    = button_measure;
	w->render     = button_render;
	w->on_key     = button_on_key;
	w->color_pair = COLOR_PAIR_BUTTON;

	return w;
}
