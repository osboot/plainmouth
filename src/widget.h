// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <sys/queue.h>
#include <stdbool.h>

#include <curses.h>
#include <panel.h>

#include "request.h"

enum color_pair {
	COLOR_PAIR_MAIN = 1,
	COLOR_PAIR_WINDOW,
	COLOR_PAIR_BUTTON,
	COLOR_PAIR_FOCUS,
};

int simple_round(float number);
void position_center(int width, int height, int *begin_y, int *begin_x);
bool get_abs_cursor(WINDOW *target, WINDOW *win, int *cursor_y, int *cursor_x);

struct text_viewport {
	wchar_t **lines;
	int nlines;
	int ncols;
};

void viewport_create(struct text_viewport *vp, const wchar_t *text);
void viewport_free(struct text_viewport *vp);
void viewport_draw(WINDOW *win, struct text_viewport *vp, int scroll_pos);

enum widget_type {
	WIDGET_WINDOW,
	WIDGET_BORDER,
	WIDGET_LABEL,
	WIDGET_BUTTON,
	WIDGET_INPUT,
	WIDGET_METER,
	WIDGET_VBOX,
	WIDGET_HBOX,
};

struct widget;
TAILQ_HEAD(widgethead, widget);

/*
 * Sizing model:
 *   measure() computes minimum size ONLY and must not depend on layout results.
 *   layout() assigns the final usable size and must not overwrite minimum sizes.
 */
typedef void (*measure_fn)(struct widget *);
typedef void (*layout_fn)(struct widget *);
typedef void (*render_fn)(struct widget *);
typedef void (*free_data_fn)(struct widget *);
typedef int  (*input_fn)(const struct widget *, wchar_t);

enum widget_property {
	PROP_NONE = 0,
	PROP_BUTTON_STATE,
	PROP_INPUT_STATE,
	PROP_INPUT_VALUE,
	PROP_METER_TOTAL,
	PROP_METER_VALUE,
};

typedef bool (*setter_fn)(struct widget *, enum widget_property, const void *);
typedef bool (*getter_fn)(struct widget *, enum widget_property, void *);

/* Private widget-specific data */
struct widget_border;
struct widget_button;
struct widget_input;
struct widget_label;
struct widget_meter;
struct widget_textview;

enum widget_flags {
	FLAG_NONE       = 0,
	FLAG_CAN_CURSOR = 1,
	FLAG_CAN_FOCUS  = 2,
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
	int lx; /* local X offset inside parent */
	int ly; /* local Y offset inside parent */
	int w;  /* allocated width */
	int h;  /* allocated height */

	/* Measured minimum size computed by measure() */
	int min_w, min_h;

	/* User fields to enforce values */
	int req_w, req_h;

	/* Flexbox-like behaviour: per-axis */
	int flex_h;   // participates in distributing free height inside VBOX
	int flex_w;   // participates in distributing free width  inside HBOX

	int grow_h;   // how strongly widget grows if free height exists (default 1)
	int grow_w;   // grow on width
	int shrink_h; // how strongly it shrinks if space insufficient (default 1)
	int shrink_w; // shrink on width

	/* ncurses objects */
	WINDOW *win;                /* Associated window (root or derived) */
	enum color_pair color_pair; /* Simple color/style attribute */
	bool visible;               /* Rendering enabled flag */
	bool in_focus;              /* Is this subtree in focus */
	bool show_cursor;

	int widget_flags;

	/* Virtual methods */
	free_data_fn free_data; /* Free widget-specific data */
	measure_fn measure;     /* Compute intrinsic minimum size */
	layout_fn layout;       /* Assign positions/sizes to children */
	render_fn render;       /* Draw contents into win */
	input_fn input;         /* Handle keyboard input */
	setter_fn setter;
	getter_fn getter;

	/* Tree structure */
	struct widget *parent;      /* Parent widget */
	struct widgethead children; /* Ordered list of child widgets */

	/* Widget-specific state */
	union {
		struct widget_border   *border;
		struct widget_button   *button;
		struct widget_input    *input;
		struct widget_label    *label;
		struct widget_meter    *meter;
		struct widget_textview *textview;
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

static inline bool widget_get(struct widget *w, enum widget_property prop, void *value)
{
	return (w->getter) ? w->getter(w, prop, value) : false;
}

static inline bool widget_set(struct widget *w, enum widget_property prop, const void *value)
{
	return (w->setter) ? w->setter(w, prop, value) : false;
}

typedef bool (*walk_fn)(struct widget *, void *);

bool walk_widget_tree(struct widget *w, walk_fn handler, void *data);

void widget_measure_tree(struct widget *w);
void widget_layout_tree(struct widget *w, int lx, int ly, int width, int height);
void widget_create_tree(struct widget *w);
void widget_render_tree(struct widget *w);

struct widget *make_window(void);
struct widget *make_vbox(void);
struct widget *make_hbox(void);
struct widget *make_label(const wchar_t *text);
struct widget *make_textview(const wchar_t *text);
struct widget *make_button(const wchar_t *label);
struct widget *make_input(const wchar_t *placeholder);
struct widget *make_input_password(const wchar_t *placeholder);
struct widget *make_meter(int total);

struct widget *make_border(void);
struct widget *make_border_vbox(struct widget *parent);
struct widget *make_border_hbox(struct widget *parent);

struct widget *find_widget_by_id(struct widget *w, int id);

void vbox_measure(struct widget *w);
void vbox_layout(struct widget *w);

#endif /* _PLAINMOUTH_WIDGET_H_ */
