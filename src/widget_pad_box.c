// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

struct widget_pad_box {
	WINDOW *pad;
	int content_h, content_w;
	int scroll_y, scroll_x;
};

static void pad_box_clamp_scroll(struct widget *pad) __attribute__((nonnull(1)));
static void pad_box_measure(struct widget *w) __attribute__((nonnull(1)));
static void pad_box_layout(struct widget *w) __attribute__((nonnull(1)));
static void pad_box_render(struct widget *w) __attribute__((nonnull(1)));
static void copy_pad_to_window(WINDOW *pad, WINDOW *win, int scroll_y, int scroll_x, int view_h, int view_w) __attribute__((nonnull(1,2)));
static bool widget_offset_in_ancestor(struct widget *ancestor, struct widget *w, int *out_y, int *out_x) __attribute__((nonnull(1,2,3,4)));
static void pad_box_ensure_visible(struct widget *container, struct widget *child) __attribute__((nonnull(1,2)));
static bool pad_box_getter(struct widget *w, enum widget_property prop, void *val) __attribute__((nonnull(1,3)));
static bool pad_box_setter(struct widget *w, enum widget_property prop, const void *val) __attribute__((nonnull(1,3)));
static void pad_box_free(struct widget *w);

void pad_box_clamp_scroll(struct widget *pad)
{
	struct widget_pad_box *st = pad->state;

	int max_scroll_y = st->content_h - pad->h;
	int max_scroll_x = st->content_w - pad->w;

	if (max_scroll_y < 0) max_scroll_y = 0;
	if (max_scroll_x < 0) max_scroll_x = 0;

	st->scroll_y = CLAMP(st->scroll_y, 0, max_scroll_y);
	st->scroll_x = CLAMP(st->scroll_x, 0, max_scroll_x);
}

void pad_box_measure(struct widget *w)
{
	w->min_h  = 1;
	w->min_w  = 1;

	w->pref_h = 0;
	w->pref_w = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = (c->pref_w > 0) ? c->pref_w : c->min_w;

		w->pref_h += ch;
		w->pref_w = MAX(w->pref_w, cw);
	}
}

void pad_box_layout(struct widget *w)
{
	struct widget_pad_box *st = w->state;

	st->content_h = 0;
	st->content_w = 0;

	int y = 0;
	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : (c->pref_w > 0 ? c->pref_w : c->min_w);

		widget_layout_tree(c, 0, y, cw, ch);
		y += ch;

		st->content_h += ch;
		st->content_w = MAX(st->content_w, cw);
	}

	pad_box_clamp_scroll(w);
}

static WINDOW *pad_box_child_render_win(struct widget *w)
{
	struct widget_pad_box *st = w->state;

	if (!st->pad) {
		st->pad = newpad(MAX(1, st->content_h), MAX(1, st->content_w));
		if (!st->pad) {
			warnx("unable to create %s pad window (y=%d, x=%d, height=%d, width=%d)",
				widget_type(w), w->ly, w->lx, st->content_h, st->content_w);
			return NULL;
		}

		if (IS_DEBUG())
			warnx("%s (%p) pad screen created (y=%d, x=%d, height=%d, width=%d) for window %p",
				widget_type(w), st->pad, w->ly, w->lx, st->content_h, st->content_w, w->win);
	}

	return st->pad;
}

void copy_pad_to_window(WINDOW *pad, WINDOW *win, int scroll_y, int scroll_x, int view_h, int view_w)
{
	cchar_t *row __free(ptr) = malloc(sizeof(cchar_t) * (size_t) (view_w + 1));
	if (!row)
		return;

	for (int y = 0; y < view_h; y++) {
		int py = scroll_y + y;

		if (mvwin_wchnstr(pad, py, scroll_x, row, view_w) == ERR) {
			for (int i = 0; i < view_w; i++)
				setcchar(&row[i], L" ", 0, 0, NULL);
		}

		wmove(win, y, 0);
		mvwadd_wchnstr(win, y, 0, row, view_w);
	}
}

void pad_box_render(struct widget *w)
{
	struct widget_pad_box *st = w->state;

	/*
	 * Impotant:
	 *  - pad is fully redrawn on each render()
	 *  - copy_pad_to_window overwrites entire viewport
	 * Therefore no werase() needed for pad or window.
	 */
	copy_pad_to_window(st->pad, w->win, st->scroll_y, st->scroll_x, w->h, w->w);
}

bool widget_offset_in_ancestor(struct widget *ancestor, struct widget *w, int *out_y, int *out_x)
{
	int y = 0, x = 0;

	while (w && w != ancestor) {
		y += w->ly;
		x += w->lx;
		w = w->parent;
	}

	if (w != ancestor)
		return false;

	*out_y = y;
	*out_x = x;
	return true;
}

void pad_box_ensure_visible(struct widget *container, struct widget *child)
{
	struct widget_pad_box *st = container->state;

	int cy, cx;
	if (!widget_offset_in_ancestor(container, child, &cy, &cx))
		return;

	bool changed = false;

	if (cy < st->scroll_y) {
		st->scroll_y = cy;
		changed = true;
	} else if (cy + child->h > st->scroll_y + container->h) {
		st->scroll_y = cy + child->h - container->h;
		changed = true;
	}

	if (cx < st->scroll_x) {
		st->scroll_x = cx;
		changed = true;
	} else if (cx + child->w > st->scroll_x + container->w) {
		st->scroll_x = cx + child->w - container->w;
		changed = true;
	}

	if (changed) {
		pad_box_clamp_scroll(container);
	}
}


bool pad_box_getter(struct widget *w, enum widget_property prop, void *val)
{
	struct widget_pad_box *st = w->state;

	switch (prop) {
		case PROP_SCROLL_X:
			*(int *) val = st->scroll_x;
			return true;
		case PROP_SCROLL_Y:
			*(int *) val = st->scroll_y;
			return true;
		case PROP_SCROLL_CONTENT_H:
			*(int *) val = st->content_h;
			return true;
		case PROP_SCROLL_CONTENT_W:
			*(int *) val = st->content_w;
			return true;
		default:
			errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

bool pad_box_setter(struct widget *w, enum widget_property prop, const void *val)
{
	struct widget_pad_box *st = w->state;

	int max_scroll_y = st->content_h - w->h;
	int max_scroll_x = st->content_w - w->w;

	if (max_scroll_y < 0) max_scroll_y = 0;
	if (max_scroll_x < 0) max_scroll_x = 0;

	switch (prop) {
		case PROP_SCROLL_X:
			st->scroll_x = CLAMP(*(int *) val, 0, max_scroll_x);
			return true;
		case PROP_SCROLL_Y:
			st->scroll_y = CLAMP(*(int *) val, 0, max_scroll_y);
			return true;
		case PROP_SCROLL_INC_X:
			st->scroll_x += *(int *) val;
			st->scroll_x = CLAMP(st->scroll_x, 0, max_scroll_x);
			return true;
		case PROP_SCROLL_INC_Y:
			st->scroll_y += *(int *) val;
			st->scroll_y = CLAMP(st->scroll_y, 0, max_scroll_y);
			return true;
		default:
			break;
	}
	return false;
}

void pad_box_free(struct widget *w)
{
	struct widget_pad_box *st = w->state;

	if (st->pad)
		delwin(st->pad);

	free(st);
}

static const struct widget_ops pad_box_ops = {
	.measure          = pad_box_measure,
	.layout           = pad_box_layout,
	.render           = pad_box_render,
	.finalize_render  = pad_box_render,
	.child_render_win = pad_box_child_render_win,
	.free             = pad_box_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = pad_box_ensure_visible,
	.setter           = pad_box_setter,
	.getter           = pad_box_getter,
	.getter_index     = NULL,
};

struct widget *make_pad_box(void)
{
	struct widget *w = widget_create(WIDGET_PAD_BOX);

	struct widget_pad_box *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_pad_box: calloc");
		widget_free(w);
		return NULL;
	}

	w->state = st;

	w->ops = &pad_box_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_w = 1;
	w->flex_h = 1;

	w->shrink_w = 0;
	w->shrink_h = 0;

	w->stretch_w = 1;
	w->stretch_h = 1;

	return w;
}
