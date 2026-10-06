// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <err.h>
#include <errno.h>
#include <limits.h>

#include <curses.h>
#include <panel.h>

#include "macros.h"
#include "widget.h"

static attr_t style_attrs[COLOR_PAIR_INVALID + 1];

int widget_dispatch_input(struct widget *w, wchar_t key, bool keycode)
{
	if (!w)
		return 0;
	w->flags &= ~FLAG_REJECTED;
	if (!widget_is_interactive(w))
		return 0;
	int handled = 0;
	if (w->ops && w->ops->input_event)
		handled = w->ops->input_event(w, key, keycode);
	else if (w->ops && w->ops->input)
		handled = w->ops->input(w, key);
	if (handled < 0) {
		w->flags |= FLAG_REJECTED;
		return 1;
	}
	if (handled || !keycode)
		return handled;

	switch (key) {
		case KEY_UP:
		case KEY_DOWN:
		case KEY_PPAGE:
		case KEY_NPAGE:
			break;
		default:
			return 0;
	}
	for (struct widget *parent = w->parent; parent; parent = parent->parent) {
		if (parent->type == WIDGET_SCROLL_VBOX)
			return parent->ops->input_event(parent, key, true);
	}
	return 0;
}

bool widget_is_interactive(const struct widget *w)
{
	for (; w; w = w->parent) {
		if (w->attrs & (ATTR_READONLY | ATTR_DISABLED))
			return false;
	}
	return true;
}

struct color_slot {
	TAILQ_ENTRY(color_slot)
	entries;
	short pair;
};
TAILQ_HEAD(color_slots, color_slot);
static struct color_slots color_slots = TAILQ_HEAD_INITIALIZER(color_slots);
static int next_color_pair = COLOR_PAIR_INVALID + 1;

struct style_override {
	int fg, bg, current_fg, current_bg;
	attr_t attrs;
	short pair;
	bool has_fg, has_bg, has_attrs;
};

struct widget_styles {
	struct style_override roles[COLOR_PAIR_INVALID + 1];
};

short widget_color_pair_alloc(void)
{
	for (int pair = next_color_pair; pair < COLOR_PAIRS && pair <= SHRT_MAX; pair++) {
		struct color_slot *slot;
		TAILQ_FOREACH(slot, &color_slots, entries)
		{
			if (slot->pair == pair)
				break;
		}
		if (slot)
			continue;
		slot = malloc(sizeof(*slot));
		if (!slot)
			return -1;
		slot->pair = (short) pair;
		TAILQ_INSERT_HEAD(&color_slots, slot, entries);
		next_color_pair = pair + 1;
		return slot->pair;
	}
	errno = ENOSPC;
	return -1;
}

void widget_color_pair_free(short pair)
{
	struct color_slot *slot;
	TAILQ_FOREACH(slot, &color_slots, entries)
	{
		if (slot->pair == pair) {
			TAILQ_REMOVE(&color_slots, slot, entries);
			free(slot);
			if (pair < next_color_pair)
				next_color_pair = pair;
			return;
		}
	}
}

static bool style_update_pair(struct style_override *style, short base_pair)
{
	int fg, bg;
	if (extended_pair_content(base_pair, &fg, &bg) == ERR)
		return false;
	if (style->has_fg)
		fg = style->fg;
	if (style->has_bg)
		bg = style->bg;
	if (fg != style->current_fg || bg != style->current_bg) {
		if (init_extended_pair(style->pair, fg, bg) == ERR)
			return false;
		style->current_fg = fg;
		style->current_bg = bg;
	}
	return true;
}

bool widget_style_override(struct widget *w, enum color_pair color,
			   const int *fg, const int *bg, const attr_t *attrs, bool reset)
{
	if (!w || color < COLOR_PAIR_WINDOW || color > COLOR_PAIR_INVALID)
		return false;
	if (reset) {
		if (w->styles) {
			struct style_override *style = &w->styles->roles[color];
			widget_color_pair_free(style->pair);
			memset(style, 0, sizeof(*style));
		}
		return true;
	}

	struct widget_styles *styles = w->styles;
	if (!styles) {
		styles = calloc(1, sizeof(*styles));
		if (!styles)
			return false;
	}
	struct style_override updated = styles->roles[color];
	short allocated = 0;
	if (fg) {
		updated.fg = *fg;
		updated.has_fg = true;
	}
	if (bg) {
		updated.bg = *bg;
		updated.has_bg = true;
	}
	if (attrs) {
		updated.attrs = *attrs;
		updated.has_attrs = true;
	}
	if ((fg || bg) && !updated.pair) {
		allocated = widget_color_pair_alloc();
		if (allocated < 0)
			goto fail;
		updated.pair = allocated;
		updated.current_fg = updated.current_bg = INT_MIN;
	}
	if (updated.pair) {
		attr_t base_attrs;
		short base_pair;
		const struct widget *base = w->parent ? w->parent : w->style_owner;
		if (!widget_style_resolve(base, color, &base_attrs, &base_pair) ||
		    !style_update_pair(&updated, base_pair))
			goto fail;
	}
	styles->roles[color] = updated;
	w->styles = styles;
	return true;

fail:
	if (allocated > 0)
		widget_color_pair_free(allocated);
	if (!w->styles)
		free(styles);
	return false;
}

bool widget_style_resolve(const struct widget *w, enum color_pair color,
			  attr_t *attrs, short *pair)
{
	*attrs = widget_style_attrs(color);
	*pair = (short) color;
	if (color < COLOR_PAIR_WINDOW || color > COLOR_PAIR_INVALID)
		return true;
	if (w) {
		const struct widget *base = w->parent ? w->parent : w->style_owner;
		if (!widget_style_resolve(base, color, attrs, pair))
			return false;
	}
	if (w && w->styles) {
		struct style_override *style = &w->styles->roles[color];
		if (style->pair) {
			if (!style_update_pair(style, *pair))
				return false;
			*pair = style->pair;
		}
		if (style->has_attrs)
			*attrs = style->attrs;
	}
	return true;
}

attr_t widget_style_attrs(enum color_pair color)
{
	if (color < COLOR_PAIR_MAIN || color > COLOR_PAIR_INVALID)
		return A_NORMAL;
	return style_attrs[color];
}

void widget_style_set_attrs(enum color_pair color, attr_t attrs)
{
	if (color >= COLOR_PAIR_MAIN && color <= COLOR_PAIR_INVALID)
		style_attrs[color] = attrs;
}

void widget_style_apply(WINDOW *win, enum color_pair color)
{
	attr_t attrs = widget_style_attrs(color);

	wbkgd(win, COLOR_PAIR(color) | attrs);
	wattr_set(win, attrs, (short) color, NULL);
}

void widget_style_apply_widget(struct widget *w, enum color_pair color)
{
	bool active_row = true;
	for (const struct widget *parent = w; parent; parent = parent->parent) {
		if (parent->type == WIDGET_SELECT_OPT && !(parent->flags & FLAG_INFOCUS))
			active_row = false;
		if (parent->attrs & ATTR_DISABLED) {
			color = COLOR_PAIR_DISABLED;
			break;
		}
		if (parent->attrs & ATTR_READONLY)
			color = COLOR_PAIR_READONLY;
		else if (active_row && (parent->flags & (FLAG_REJECTED | FLAG_INFOCUS)) ==
					       (FLAG_REJECTED | FLAG_INFOCUS))
			color = COLOR_PAIR_INVALID;
	}
	attr_t attrs;
	short pair;
	if (!widget_style_resolve(w, color, &attrs, &pair)) {
		warnx("unable to resolve widget style");
		return;
	}
	wbkgd(w->win, COLOR_PAIR(pair) | attrs);
	wattr_set(w->win, attrs, pair, NULL);
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

void distribute_flex_axis(int count, const int *pref,
		const int *min, const int *max, const int *grow,
		const int *shrink, int available, int *out)
{
	int i;
	int sum_pref = 0;

	for (i = 0; i < count; i++)
		sum_pref += pref[i];

	if (available >= sum_pref) {
		int extra = available - sum_pref;
		int sum_grow = 0;

		for (i = 0; i < count; i++)
			sum_grow += grow[i];

		int allocated = 0;
		for (i = 0; i < count; i++) {
			int add = (sum_grow > 0) ? (extra * grow[i]) / sum_grow : 0;

			out[i] = pref[i] + add;
			allocated += add;

			if (max[i] > 0 && out[i] > max[i])
				out[i] = max[i];
		}

		int rem = extra - allocated;
		for (i = 0; i < count && rem > 0; i++) {
			if (!grow[i])
				continue;

			if (max[i] == 0 || out[i] < max[i]) {
				out[i]++;
				rem--;
			}
		}
		return;
	}

	int deficit = sum_pref - available;
	for (i = 0; i < count; i++)
		out[i] = pref[i];

	bool changed = true;
	while (deficit > 0 && changed) {
		int sum_shrink_active = 0;

		changed = false;
		for (i = 0; i < count; i++)
			if (out[i] > min[i])
				sum_shrink_active += shrink[i];

		if (sum_shrink_active == 0)
			break;

		int total_cut = 0;
		for (i = 0; i < count; i++) {
			if (out[i] <= min[i])
				continue;

			int cut = (deficit * shrink[i]) / sum_shrink_active;
			int newsize = out[i] - cut;

			if (newsize < min[i])
				newsize = min[i];

			total_cut += (out[i] - newsize);

			if (newsize != out[i])
				changed = true;

			out[i] = newsize;
		}
		deficit -= total_cut;
	}

	for (i = count - 1; i >= 0 && deficit > 0; i--) {
		int take = MIN(deficit, out[i] - min[i]);
		if (take > 0) {
			out[i] -= take;
			deficit -= take;
		}
	}
}

void widget_scrollbar_draw(struct widget *w, enum color_pair color,
			   int scroll_pos, int content_size, bool vertical)
{
	WINDOW *scrollwin = w->win;
	attr_t attrs;
	short pair;
	if (!widget_style_resolve(w, color, &attrs, &pair))
		return;
	int view_w, view_h;
	getmaxyx(scrollwin, view_h, view_w);

	int view_size = vertical ? view_h : view_w;
	if ((content_size - view_size) <= 0)
		return;

	int thumb_size = MAX(1, (view_size * view_size) / content_size);
	int thumb_pos = (scroll_pos * (view_size - thumb_size)) / (content_size - view_size);

	attr_t previous_attrs;
	short previous_pair;
	wattr_get(scrollwin, &previous_attrs, &previous_pair, NULL);
	wattr_set(scrollwin, attrs, pair, NULL);
	for (int i = 0; i < view_size; i++) {
		int y = vertical ? i : view_h - 1;
		int x = vertical ? view_w - 1 : i;
		mvwaddch(scrollwin, y, x, ACS_CKBOARD);
	}
	wattr_set(scrollwin, attrs | A_REVERSE, pair, NULL);
	for (int i = 0; i < thumb_size; i++) {
		chtype c = ' ';

		if (thumb_size >= 2) {
			if (i == 0)
				c = vertical ? '^' : '<';
			else if (i == thumb_size - 1)
				c = vertical ? 'v' : '>';
		}

		int y = vertical ? thumb_pos + i : view_h - 1;
		int x = vertical ? view_w - 1 : thumb_pos + i;
		mvwaddch(scrollwin, y, x, c);
	}
	wattr_set(scrollwin, previous_attrs, previous_pair, NULL);
}

void widget_scrollbar_measure(struct widget *w, bool vertical)
{
	if (vertical) {
		w->min_w = w->max_w = w->pref_w = 1;
		w->min_h = 1;
		return;
	}

	w->min_h = w->max_h = w->pref_h = 1;
	w->min_w = 1;
}

void widget_scrollbar_render(struct widget *w, bool vertical)
{
	const struct widget_scrollbar_state *st = w->state;

	if (st->content <= st->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	widget_scrollbar_draw(w, color, st->offset, st->content, vertical);
}

static void widget_sync_scrollbar(struct widget *source, struct widget *scrollbar,
		enum widget_property offset_prop, enum widget_property content_prop,
		enum widget_property view_prop, int view_size)
{
	int offset = 0;
	int content = 0;

	if (!source || !scrollbar)
		return;

	widget_get(source, offset_prop, &offset);
	widget_get(source, content_prop, &content);

	widget_set(scrollbar, offset_prop, &offset);
	widget_set(scrollbar, content_prop, &content);
	widget_set(scrollbar, view_prop, &view_size);
}

void widget_sync_vscroll(struct widget *source, struct widget *vscroll)
{
	widget_sync_scrollbar(source, vscroll,
			PROP_SCROLL_Y, PROP_SCROLL_CONTENT_H,
			PROP_SCROLL_VIEW_H, source ? source->h : 0);
}

void widget_sync_hscroll(struct widget *source, struct widget *hscroll)
{
	widget_sync_scrollbar(source, hscroll,
			PROP_SCROLL_X, PROP_SCROLL_CONTENT_W,
			PROP_SCROLL_VIEW_W, source ? source->w : 0);
}

bool widget_scrollbar_setter(struct widget_scrollbar_state *st,
		enum widget_property prop, const void *in,
		enum widget_property content_prop,
		enum widget_property view_prop,
		enum widget_property offset_prop)
{
	if (prop == content_prop) {
		st->content = *(const int *)in;
		return true;
	}
	if (prop == view_prop) {
		st->viewport = *(const int *)in;
		return true;
	}
	if (prop == offset_prop) {
		st->offset = *(const int *)in;
		return true;
	}

	return false;
}

bool widget_scrollbar_getter(const struct widget_scrollbar_state *st,
		enum widget_property prop, void *out,
		enum widget_property offset_prop)
{
	if (prop == offset_prop) {
		*(int *)out = st->offset;
		return true;
	}
	return false;
}

void widget_scrollbar_state_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
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
		[WIDGET_TAILVIEW]    = "tailview",
		[WIDGET_TERMINAL]    = "terminal",
		[WIDGET_POSITIONED]  = "positioned",
	};
	if (!w)
		return "NULL";
	if (w->type >= 0 && w->type < WIDGET_COUNTS)
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

	if (w->state && w->ops && w->ops->free) {
		w->ops->free(w);
		w->state = NULL;
	}

	if (w->styles) {
		for (int role = COLOR_PAIR_WINDOW; role <= COLOR_PAIR_INVALID; role++)
			widget_color_pair_free(w->styles->roles[role].pair);
		free(w->styles);
	}
	free(w);
}

void widget_noutrefresh(struct widget *w)
{
	if (!w->win || is_pad(w->win))
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
static void widget_render_subtree(struct widget *w)
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

	if (w->type != WIDGET_TERMINAL)
		widget_style_apply_widget(w, w->color_pair);
	werase(w->win);

	if (w->ops && w->ops->render)
		w->ops->render(w);

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		if (c->h > 0 && c->w > 0)
			widget_render_subtree(c);
	}

	if (w->ops && w->ops->finalize_render)
		w->ops->finalize_render(w);

	widget_refresh_upper_tree(w);
}

void widget_render_tree(struct widget *w)
{
	if (!w)
		return;
	/* A pad child update must also redraw the viewport containing it. */
	struct widget *root = w;
	for (struct widget *parent = w->parent; parent; parent = parent->parent)
		if (parent->type == WIDGET_PAD_BOX)
			root = parent;
	widget_render_subtree(root);
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

struct widget *find_widget_by_type_and_id(struct widget *w, enum widget_type type, int id)
{
	if (!w)
		return NULL;

	if (w->type == type && w->w_id == id)
		return w;

	struct widget *c, *n;

	TAILQ_FOREACH(c, &w->children, siblings)
	{
		if ((n = find_widget_by_type_and_id(c, type, id)) != NULL)
			return n;
	}

	return NULL;
}

bool widget_coordinates_yx(struct widget *w, int *wy, int *wx)
{
	if (!w || !w->win || !wy || !wx)
		return false;
	int y, x;
	getyx(w->win, y, x);
	while (w->parent) {
		y += w->ly;
		x += w->lx;
		w = w->parent;
		if (w->type == WIDGET_PAD_BOX) {
			int scroll_y, scroll_x;
			if (!widget_get(w, PROP_SCROLL_Y, &scroll_y) ||
			    !widget_get(w, PROP_SCROLL_X, &scroll_x))
				return false;
			y -= scroll_y;
			x -= scroll_x;
		}
		if (y < 0 || x < 0 || y >= w->h || x >= w->w)
			return false;
	}
	int ry, rx;
	getbegyx(w->win, ry, rx);
	*wy = ry + y;
	*wx = rx + x;

	return true;
}
