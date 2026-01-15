// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

#ifndef CLAMP
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

struct widget_pad_box {
	int content_h, content_w;
	int scroll_y, scroll_x;
};

static void pad_box_clamp_scroll(struct widget *pad)
{
	struct widget_pad_box *st = pad->state.pad_box;

	int max_scroll_y = st->content_h - pad->h;
	int max_scroll_x = st->content_w - pad->w;

	if (max_scroll_y < 0) max_scroll_y = 0;
	if (max_scroll_x < 0) max_scroll_x = 0;

	st->scroll_y = CLAMP(st->scroll_y, 0, max_scroll_y);
	st->scroll_x = CLAMP(st->scroll_x, 0, max_scroll_x);
}

static void pad_box_measure(struct widget *w)
{
	struct widget_pad_box *st = w->state.pad_box;

	int content_h = 0;
	int content_w = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = (c->pref_w > 0) ? c->pref_w : c->min_w;

		content_h += ch;
		content_w = MAX(content_w, cw);
	}

	st->content_h = content_h;
	st->content_w = content_w;

	w->min_h  = 1;
	w->min_w  = 1;

	w->pref_h = content_h;
	w->pref_w = content_w;
}

static void pad_box_layout(struct widget *w)
{
	struct widget_pad_box *st = w->state.pad_box;

	st->content_h = 0;
	st->content_w = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : (c->pref_w > 0 ? c->pref_w : c->min_w);

		st->content_h += ch;
		st->content_w = MAX(st->content_w, cw);
	}

	pad_box_clamp_scroll(w);

	int y = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : (c->pref_w > 0 ? c->pref_w : c->min_w);

		widget_layout_tree(c, 0, y, cw, ch);
		y += ch;
	}
}

static bool pad_box_createwin(struct widget *w)
{
	struct widget_pad_box *st = w->state.pad_box;

	w->win = newpad(st->content_h, st->content_w);
	if (!w->win) {
		warnx("unable to create %s pad window (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->ly, w->lx, st->content_h, st->content_w);
		return false;
	}

	return true;
}

static void pad_box_refresh(struct widget *w)
{
	struct widget_pad_box *st = w->state.pad_box;
	int ay, ax;

	if (!widget_coordinates_yx(w->parent, &ay, &ax)) {
		warnx("unable to get scrollbar coordinates");
		return;
	}

	pnoutrefresh(w->win, st->scroll_y, st->scroll_x,
			ay + w->ly,
			ax + w->lx,
			ay + w->ly + w->h - 1,
			ax + w->lx + w->w - 1);
}

static void pad_box_ensure_visible(struct widget *container, struct widget *child)
{
	struct widget_pad_box *st = container->state.pad_box;

	int top_y = st->scroll_y;
	int bot_y = st->scroll_y + container->h;

	int left_x = st->scroll_x;
	int right_x = st->scroll_x + container->w;

	if (child->ly < top_y)
		st->scroll_y = child->ly;
	else if (child->ly + child->h > bot_y)
		st->scroll_y = child->ly + child->h - container->h;

	if (child->lx < left_x)
		st->scroll_x = child->lx;
	else if (child->lx + child->w > right_x)
		st->scroll_x = child->lx + child->w - container->w;

	pad_box_clamp_scroll(container);
}

static bool pad_box_getter(struct widget *w, enum widget_property prop, void *val)
{
	struct widget_pad_box *st = w->state.pad_box;

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

static bool pad_box_setter(struct widget *w, enum widget_property prop, const void *val)
{
	struct widget_pad_box *st = w->state.pad_box;

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

static void pad_box_free(struct widget *w)
{
	free(w->state.pad_box);
	w->state.pad_box = NULL;
}

struct widget *make_pad_box(void)
{
	struct widget *w = widget_create(WIDGET_PAD_BOX);

	struct widget_pad_box *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_pad_box: calloc");
		widget_free(w);
		return NULL;
	}

	w->state.pad_box = st;

	w->measure        = pad_box_measure;
	w->layout         = pad_box_layout;
	w->create_win     = pad_box_createwin;
	w->noutrefresh    = pad_box_refresh;
	w->ensure_visible = pad_box_ensure_visible;
	w->free_data      = pad_box_free;
	w->getter         = pad_box_getter;
	w->setter         = pad_box_setter;

	w->flex_w = 1;
	w->flex_h = 1;

	w->shrink_w = 0;
	w->shrink_h = 0;

	w->stretch_w = 1;
	w->stretch_h = 1;

	return w;
}
