// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <sys/queue.h>
#include <stdbool.h>

#include <curses.h>
#include <panel.h>

#include "request.h"
#include "warray.h"

/*
 * ---------------------------------------------------------------------------
 *  Widget Sizing Model and Container Layout Semantics
 * ---------------------------------------------------------------------------
 *
 * The layout engine follows a two-phase model:
 *
 * 1. measure()  → computes intrinsic size requirements
 * 2. layout()   → assigns final geometry for each widget
 *
 * During measure(), each widget sets:
 *
 * min_w / min_h   - the smallest acceptable size
 * pref_w / pref_h - the preferred (content-based) size
 * max_w / max_h   - the largest acceptable size (INT_MAX = unlimited)
 *
 * During layout(), containers distribute the available space according to
 * Flexbox-inspired rules:
 *
 * - The *main axis* is the direction in which children are arranged:
 *   - HBOX: horizontal main axis
 *   - VBOX: vertical main axis
 *
 * - The *cross axis* is perpendicular to the main axis.
 *
 * Containers use the following widget fields when distributing space:
 *
 * flex_w / flex_h
 *  - Controls how a widget shares extra space along the main axis.
 *  - If multiple children have flex > 0, free space is divided proportionally
 *    to their flex values.
 *
 * shrink_w / shrink_h
 *  - Controls how a widget gives up space when the container is smaller than
 *    the sum of preferred sizes.
 *  - If multiple children have shrink > 0, they reduce size proportionally
 *    until reaching their min_* limits.
 *
 * stretch_w / stretch_h
 *  - Controls whether a widget expands along the cross axis.
 *  - If stretch is true, the widget receives the full cross-axis size of its
 *    container; otherwise, it receives pref_* clamped to [min_*, max_*].
 */

enum color_pair {
	COLOR_PAIR_MAIN = 1,
	COLOR_PAIR_WINDOW,
	COLOR_PAIR_BUTTON,
	COLOR_PAIR_FOCUS,
};

int simple_round(float number);
void position_center(int width, int height, int *begin_y, int *begin_x);
bool get_abs_cursor(WINDOW *target, WINDOW *win, int *cursor_y, int *cursor_x);
int w_mvprintw(WINDOW *win, int y, int x, const wchar_t *fmt, ...);
void w_addch(WINDOW *win, wchar_t wc);

enum widget_type {
	WIDGET_WINDOW,
	WIDGET_BORDER,
	WIDGET_LABEL,
	WIDGET_BUTTON,
	WIDGET_CHECKBOX,
	WIDGET_INPUT,
	WIDGET_METER,
	WIDGET_VBOX,
	WIDGET_HBOX,
	WIDGET_TOOLTIP,
	WIDGET_LIST_VBOX,
	WIDGET_SELECT,
	WIDGET_SELECT_OPT,
	WIDGET_SPINBOX,
	WIDGET_SCROLL_VBOX,
	WIDGET_HSCROLL,
	WIDGET_VSCROLL,
	WIDGET_PAD_BOX,
};

struct widget;
TAILQ_HEAD(widgethead, widget);

enum widget_property {
	PROP_NONE = 0,
	PROP_BUTTON_STATE,
	PROP_CHECKBOX_STATE,
	PROP_INPUT_STATE,
	PROP_INPUT_VALUE,
	PROP_METER_TOTAL,
	PROP_METER_VALUE,
	PROP_SELECT_OPTIONS_SIZE,
	PROP_SELECT_OPTION_VALUE,
	PROP_SELECT_CURSOR,
	PROP_SPINBOX_VALUE,
	PROP_SCROLL_CONTENT_H,
	PROP_SCROLL_CONTENT_W,
	PROP_SCROLL_VIEW_W,
	PROP_SCROLL_VIEW_H,
	PROP_SCROLL_INC_X,
	PROP_SCROLL_INC_Y,
	PROP_SCROLL_X,
	PROP_SCROLL_Y,
};

/* Private widget-specific data */
struct widget_border;
struct widget_button;
struct widget_checkbox;
struct widget_hscroll;
struct widget_input;
struct widget_label;
struct widget_list_vbox;
struct widget_meter;
struct widget_pad_box;
struct widget_select;
struct widget_spinbox;
struct widget_svbox;
struct widget_textview;
struct widget_tooltip;
struct widget_vscroll;

enum widget_flags {
	FLAG_NONE    = 0,        // Nothing has been set
	FLAG_CREATED = (1 << 0), // Rendering enabled flag
	FLAG_INFOCUS = (1 << 1), // Is this subtree in focus
	FLAG_VISIBLE = (1 << 2),
};

enum widget_attributes {
	ATTR_NONE       = 0,        // Nothing has been set
	ATTR_CAN_CURSOR = (1 << 0), // Cursor may be displayed in the widget
	ATTR_CAN_FOCUS  = (1 << 1), // Widget can be in focus
};

/*
 * Generic UI widget used in the ncurses-based layout system. Widgets form
 * a tree: each widget may contain children.
 *
 * Layout is computed in two phases:
 *   - measure(): compute minimum required size
 *   - layout(): assign final position and size
 */
struct widget {
	TAILQ_ENTRY(widget) siblings;
	TAILQ_ENTRY(widget) focuses;

	/* Plugin block */
	const char *instance_id;

	/* Widget kind (label, button, container, etc.) */
	enum widget_type type;

	/* Geometry relative to the parent widget */
	int lx, ly; /* local X / Y offset inside parent */
	int w, h;  /* allocated width / height */

	/* Measured minimum size computed by measure() */
	int min_w, min_h;

	/* Flexbox-like behaviour: per-axis */
	int flex_h;   // participates in distributing free height inside VBOX
	int flex_w;   // participates in distributing free width  inside HBOX
	int shrink_h; // how strongly it shrinks if space insufficient (default 1)
	int shrink_w; // shrink on width

	/*
	 * Preferred size semantics:
	 *   pref_* — preferred/base size used as starting point for flex grow/shrink.
	 *            If pref == 0 it will be treated as pref = min.
	 *   max_*  — optional maximum size (0 means unlimited).
	 */
	int pref_w, pref_h;
	int max_w, max_h;

	/*
	 * Stretch flags (cross-axis):
	 *   When true, this child stretches to fill the cross axis of the container.
	 */
	bool stretch_w, stretch_h;

	/* ncurses objects */
	WINDOW *win;                /* Associated window (root or derived) */
	enum color_pair color_pair; /* Simple color/style attribute */
	int attrs;                  /* current attributes (widget_arributes) */
	int flags;                  /* (widget_flags) */

	/* Virtual methods */
	void (*measure)(struct widget *);              /* Compute intrinsic minimum size */
	void (*layout)(struct widget *);               /* Assign positions/sizes to children */
	void (*render)(struct widget *);               /* Draw contents into win */
	bool (*create_win)(struct widget *);           /* Custom function to create window */
	void (*noutrefresh)(struct widget *);          /* Custom function to refresh curses window */
	void (*free_data)(struct widget *);            /* Free widget-specific data */
	int  (*input)(const struct widget *, wchar_t); /* Handle keyboard input */

	void (*add_child)(struct widget *parent, struct widget *child);
	void (*ensure_visible)(struct widget *, struct widget *);

	bool (*setter)(struct widget *, enum widget_property, const void *);
	bool (*getter)(struct widget *, enum widget_property, void *);
	bool (*getter_index)(struct widget *, enum widget_property, int, void *);

	/* Tree structure */
	struct widget *parent;      /* Parent widget */
	struct widgethead children; /* Ordered list of child widgets */

	/* Widget-specific state */
	union {
		struct widget_border    *border;
		struct widget_button    *button;
		struct widget_checkbox  *checkbox;
		struct widget_hscroll   *hscroll;
		struct widget_input     *input;
		struct widget_label     *label;
		struct widget_list_vbox *list_vbox;
		struct widget_meter     *meter;
		struct widget_pad_box   *pad_box;
		struct widget_select    *select;
		struct widget_spinbox   *spinbox;
		struct widget_svbox     *svbox;
		struct widget_textview  *textview;
		struct widget_tooltip   *tooltip;
		struct widget_vscroll   *vscroll;
	} state;

	/*
	 * User data. This is usually the global context of the entire instance,
	 * which is used by event handlers.
	 */
	void *data;

	/* Optional user' widget ID  */
	int w_id;
};

const char *widget_type(struct widget *w);
struct widget *widget_create(enum widget_type);
void widget_add(struct widget *parent, struct widget *child);
void widget_free(struct widget *w);
bool widget_coordinates_yx(struct widget *w, int *w_abs_y, int *w_abs_x);
void widget_noutrefresh(struct widget *w);
void widget_dump(FILE *fd, struct widget *w);

static inline bool widget_get(struct widget *w, enum widget_property prop, void *value)
{
	return (w->getter) ? w->getter(w, prop, value) : false;
}

static inline bool widget_get_index(struct widget *w, enum widget_property prop, int index, void *value)
{
	return (w->getter_index) ? w->getter_index(w, prop, index, value) : false;
}

static inline bool widget_set(struct widget *w, enum widget_property prop, const void *value)
{
	return (w->setter) ? w->setter(w, prop, value) : false;
}

typedef bool (*walk_fn)(struct widget *, void *);

bool walk_widget_tree(struct widget *w, walk_fn handler, void *data);

void widget_measure_tree(struct widget *w);
void widget_layout_tree(struct widget *w, int lx, int ly, int width, int height);
void widget_hide_tree(struct widget *w);
void widget_render_tree(struct widget *w);

struct widget *make_window(void);
struct widget *make_vbox(void);
struct widget *make_hbox(void);
struct widget *make_vscroll(void);
struct widget *make_hscroll(void);
struct widget *make_scroll_vbox(void);
struct widget *make_pad_box(void);
struct widget *make_label(const wchar_t *text);
struct widget *make_textview(const wchar_t *text);
struct widget *make_button(const wchar_t *label);
struct widget *make_checkbox(bool checked, bool is_radio);
struct widget *make_input(const wchar_t *initdata, const wchar_t *placeholder);
struct widget *make_input_password(const wchar_t *initdata, const wchar_t *placeholder);
struct widget *make_meter(int total);
struct widget *make_tooltip(const wchar_t *line);
struct widget *make_spinbox(int min, int max, int step, int initial, int width);
struct widget *make_list_vbox(int view_rows);

struct widget *make_select(int max_selected, int view_rows);
struct widget *make_select_option(const wchar_t *text, bool checked, bool is_radio);

struct widget *make_border(void);
struct widget *make_border_vbox(struct widget *parent);
struct widget *make_border_hbox(struct widget *parent);

struct widget *find_widget_by_id(struct widget *w, int id);

void vbox_measure(struct widget *w);
void vbox_layout(struct widget *w);

#endif /* _PLAINMOUTH_WIDGET_H_ */
