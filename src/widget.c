// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

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

int w_mvprintw(WINDOW *win, int y, int x, const wchar_t *fmt, ...)
{
	wchar_t *buf __free(ptr) = NULL;
	size_t len = 0;

	FILE *f = open_wmemstream(&buf, &len);
	if (!f)
		return ERR;

	va_list ap;
	va_start(ap, fmt);
	int rc = vfwprintf(f, fmt, ap);
	va_end(ap);

	fclose(f);

	if (rc == -1)
		return ERR;

	mvwaddwstr(win, y, x, buf);

	return OK;
}

void w_addch(WINDOW *win, wchar_t wc)
{
	cchar_t cc;
	wchar_t s[2] = { wc, L'\0' };

	setcchar(&cc, s, 0, 0, NULL);
	wadd_wch(win, &cc);
}

const char *widget_type(struct widget *w)
{
	static const char *_widget_type[] = {
		[WIDGET_WINDOW]      = "window",
		[WIDGET_BORDER]      = "border",
		[WIDGET_LABEL]       = "label",
		[WIDGET_BUTTON]      = "button",
		[WIDGET_CHECKBOX]    = "checkbox",
		[WIDGET_INPUT]       = "input",
		[WIDGET_METER]       = "meter",
		[WIDGET_VBOX]        = "vbox",
		[WIDGET_HBOX]        = "hbox",
		[WIDGET_TOOLTIP]     = "tooltip",
		[WIDGET_LIST_VBOX]   = "list_vbox",
		[WIDGET_SELECT]      = "select",
		[WIDGET_SELECT_OPT]  = "select_option",
		[WIDGET_SPINBOX]     = "spinbox",
		[WIDGET_SCROLL_VBOX] = "scroll_vbox",
		[WIDGET_VSCROLL]     = "vscroll",
		[WIDGET_HSCROLL]     = "hscroll",
		[WIDGET_PAD_BOX]     = "pad_box",
	};
	if (!w)
		return "NULL";
	if (w->type >= 0 || w->type < WIDGET_COUNTS)
		return _widget_type[w->type];
	return "unknown";
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

	w->flags |= FLAG_CREATED | FLAG_VISIBLE;
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
	if (!parent || !child)
		return;

	child->parent = parent;

	if (parent->ops && parent->ops->add_child) {
		parent->ops->add_child(parent, child);
		return;
	}

	TAILQ_INSERT_TAIL(&parent->children, child, siblings);
}

static void widget_destroy_window(struct widget *w);

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

	widget_destroy_window(w);

	if (w->ops && w->ops->free_data) {
		w->ops->free_data(w);
		w->state = NULL;
	}

	free(w);
}

void widget_noutrefresh(struct widget *w)
{
	if (!w->win)
		return;

	wnoutrefresh(w->win);
}

static void widget_refresh_upper_tree(struct widget *w)
{
	if (w) {
		widget_noutrefresh(w);
		widget_refresh_upper_tree(w->parent);
	}
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

	if (w->ops && w->ops->measure)
		w->ops->measure(w);

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

	if (width  >= 0) w->w = width;
	if (height >= 0) w->h = height;

	if (w->ops && w->ops->layout)
		w->ops->layout(w);
}

static void widget_create_window(struct widget *w)
{
	WINDOW *parent_win = NULL;

	if (w->parent == NULL) {
		/* root: absolute coords */
		w->win = newwin(w->h, w->w, w->ly, w->lx);
		if (!w->win) {
			warnx("unable to create %s window (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, w->h, w->w);
		}
	} else if (!w->parent->win) {
		warnx("unable to create %s subwindow without parent window (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->ly, w->lx, w->h, w->w);
	} else {
		/* child: derived window */
		parent_win = (w->parent->ops && w->parent->ops->child_render_win)
			? w->parent->ops->child_render_win(w->parent)
			: w->parent->win;

		w->win = derwin(parent_win, w->h, w->w, w->ly, w->lx);

		if (!w->win) {
			warnx("unable to create %s subwindow (y=%d, x=%d, height=%d, width=%d) in parent win %p",
				widget_type(w), w->ly, w->lx, w->h, w->w, parent_win);
		}
	}

	if (!w->win) {
		w->flags &= ~FLAG_CREATED;
		return;
	}

	if (IS_DEBUG()) {
		if (w->parent)
			warnx("%s (%p) subwindow was created (y=%d, x=%d, height=%d, width=%d) in parent win %p",
				widget_type(w), w->win, w->ly, w->lx, w->h, w->w, parent_win);
		else
			warnx("%s (%p) window was created (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->win, w->ly, w->lx, w->h, w->w);
	}

	if (w->color_pair)
		wbkgd(w->win, COLOR_PAIR(w->color_pair));

	w->flags |= FLAG_CREATED;
}

static void widget_destroy_window(struct widget *w)
{
	if (!w || !w->win)
		return;

	if (delwin(w->win) == ERR) {
		warnx("unable to destroy ncurses win of widget %s (%p) (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->win, w->ly, w->lx, w->h, w->w);
		return;
	}

	if (IS_DEBUG())
		warnx("destroy ncurses win of widget %s (%p) (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->win, w->ly, w->lx, w->h, w->w);

	w->win = NULL;
	w->flags &= ~FLAG_CREATED;
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
	if (!w)
		return;

	if (!(w->flags & FLAG_VISIBLE))
		return;

	if (w->win && w->parent) {
		int wy, wx;
		getparyx(w->win, wy, wx);

		if (w->ly != wy || w->lx != wx) {
			/*
			 * mvderwin does not work for some reason. There are no
			 * errors, but the window does not move.
			 */
			widget_hide_tree(w);
		}
	}

	if (!w->win) {
		widget_create_window(w);
		if (!w->win)
			return;
	}

	werase(w->win);

	if (w->ops && w->ops->render)
		w->ops->render(w);

	widget_refresh_upper_tree(w);

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->h > 0 && c->w > 0)
			widget_render_tree(c);
	}

	if (w->ops && w->ops->finalize_render)
		w->ops->finalize_render(w);
}

void widget_hide_tree(struct widget *w)
{
	if (!w)
		return;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		widget_hide_tree(c);
	}

	widget_destroy_window(w);
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

bool widget_coordinates_yx(struct widget *w, int *wy, int *wx)
{
	int y, x, ry, rx;
	struct widget *root = w;

	while (root->parent)
		root = root->parent;

	if (!get_abs_cursor(root->win, w->win, &y, &x))
		return false;

	getbegyx(root->win, ry, rx);

	*wy = ry + y;
	*wx = rx + x;

	return true;
}
