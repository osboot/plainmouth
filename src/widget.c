// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>
#include <panel.h>

#include "macros.h"
#include "widget.h"

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

/*
 * ############################################################################
 */

const char *widget_type(struct widget *w)
{
	static const char *_widget_type[] = {
		[WIDGET_WINDOW] = "window",
		[WIDGET_BORDER] = "border",
		[WIDGET_LABEL]  = "label",
		[WIDGET_BUTTON] = "button",
		[WIDGET_INPUT]  = "input",
		[WIDGET_METER]  = "meter",
		[WIDGET_VBOX]   = "vbox",
		[WIDGET_HBOX]   = "hbox",
	};
	return _widget_type[w->type];
}

/*
 * Allocate a new widget of the given type.
 * Initializes fields to safe defaults and resets child list.
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

	w->flags |= FLAG_VISIBLE;
	w->color_pair = COLOR_PAIR_MAIN;

	w->flex_h   = w->flex_w   = 0;
	w->shrink_h = w->shrink_w = 1;

	return w;
}

/*
 * Attach a child widget to a parent.
 * Does not affect geometry; the caller must rerun measure/layout.
 */
void widget_add(struct widget *parent, struct widget *child)
{
	child->parent = parent;
	TAILQ_INSERT_TAIL(&parent->children, child, siblings);
}

/*
 * Recursively destroy a widget and all its descendants.
 */
void widget_free(struct widget *w)
{
	if (!w)
		return;

	while (!TAILQ_EMPTY(&w->children)) {
		struct widget *c = TAILQ_FIRST(&w->children);
		TAILQ_REMOVE(&w->children, c, siblings);
		widget_free(c);
	}

	if (IS_DEBUG())
		warnx("destroy widget %s (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, w->h, w->w);

	if (w->win) {
		delwin(w->win);
		w->win = NULL;
	}

	if (w->free_data)
		w->free_data(w);

	free(w);
}

/*
 * Recursively compute minimum size for a widget subtree.
 *
 * Result: Each widget has known min_w and min_h.
 */
void widget_measure_tree(struct widget *w)
{
	if (!w)
		return;

	struct widget *c;

	TAILQ_FOREACH(c, &w->children, siblings)
		widget_measure_tree(c);

	if (w->measure)
		w->measure(w);

	/*
	 * Ensure preferred sizes are at least minimum. If pref is unset (0),
	 * treat pref as min. This makes preferred available for flex algs.
	 */
	w->pref_w = MAX(w->pref_w, w->min_w);
	w->pref_h = MAX(w->pref_h, w->min_h);
}

/*
 * Assign final geometry to the widget.
 *
 * lx, ly, width, height:
 *
 * If >= 0, update the corresponding field.
 * If < 0, keep previous values.
 *
 * Result: Each widget knows: lx, ly, w, h.
 */
void widget_layout_tree(struct widget *w, int lx, int ly, int width, int height)
{
	if (!w)
		return;

	if (lx >= 0) w->lx = lx;
	if (ly >= 0) w->ly = ly;

	if (width  > 0) w->w = width;
	if (height > 0) w->h = height;

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
			warnx("unable to create %s window (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, w->h, w->w);
			return;
		}
	} else {
		/* child: derived window */
		if (!w->parent->win) {
			warnx("unable to create %s subwindow without parent window (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, w->h, w->w);
			return;
		}

		w->win = derwin(w->parent->win, w->h, w->w, w->ly, w->lx);
		if (!w->win) {
			warnx("unable to create %s subwindow (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, w->h, w->w);
			w->flags &= ~FLAG_VISIBLE;
			return;
		}
	}
	if (IS_DEBUG())
		warnx("%s (%p) window was created (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->win, w->ly, w->lx, w->h, w->w);

	if (w->color_pair)
		wbkgd(w->win, COLOR_PAIR(w->color_pair));

	w->flags |= FLAG_VISIBLE;
}

/*
 * Ensure that each widget's WINDOW matches the assigned geometry. If size or
 * position changed, recreate the window.
 */
void widget_create_tree(struct widget *w)
{
	if (!w)
		return;

	if (widget_maybe_recreate(w))
		widget_create_window(w);

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings)
		widget_create_tree(c);
}

/*
 * Draw the widget subtree.
 *
 * Rendering order:
 *   1. Parent draws itself
 *   2. Then children are rendered
 *
 * render() hook should draw into w->win but not call wrefresh().
 * This function uses wnoutrefresh() so caller can call doupdate().
 */
void widget_render_tree(struct widget *w)
{
	if (!w || !(w->flags & FLAG_VISIBLE))
		return;

	if (w->win) {
		werase(w->win);

		if (w->render)
			w->render(w);

		wnoutrefresh(w->win);
	}

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings)
		widget_render_tree(c);
}

bool walk_widget_tree(struct widget *w, walk_fn handler, void *data)
{
	if (!w)
		return false;

	if (!handler(w, data))
		return false;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		if (!walk_widget_tree(c, handler, data))
			return false;
	}

	return true;
}

struct widget *find_widget_by_id(struct widget *w, int id)
{
	if (!w)
		return NULL;

	if (w->w_id == id)
		return w;

	struct widget *c, *n;

	TAILQ_FOREACH(c, &w->children, siblings) {
		if ((n = find_widget_by_id(c, id)) != NULL)
			return n;
	}

	return NULL;
}
