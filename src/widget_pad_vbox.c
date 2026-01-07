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

struct widget_pad_vbox {
	int content_h;
	int content_w;
	int scroll;
};

static void pad_vbox_measure(struct widget *w)
{
	struct widget *c;

	w->min_h  = w->min_w  = 0;
	w->pref_h = w->pref_w = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		w->min_h  += c->min_h;
		w->pref_h += c->pref_h;

		w->min_w  = MAX(w->min_w,  c->min_w);
		w->pref_w = MAX(w->pref_w, c->pref_w);
	}

	w->min_h = MIN(w->min_h, 1);
}

static void pad_vbox_layout(struct widget *w)
{
	struct widget_pad_vbox *st = w->state.pad_vbox;

	st->content_h = 0;
	st->content_w = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : c->min_w;

		st->content_h += ch;
		st->content_w = MAX(st->content_w, cw);
	}

	int max_scroll = st->content_h - w->h;
	if (max_scroll < 0)
		max_scroll = 0;

	st->scroll = CLAMP(st->scroll, 0, max_scroll);

	int y = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : c->min_w;

		widget_layout_tree(c, 0, y, cw, ch);
		y += ch;
	}
}

static bool pad_vbox_createwin(struct widget *w)
{
	struct widget_pad_vbox *st = w->state.pad_vbox;

	w->win = newpad(st->content_h, st->content_w);
	if (!w->win) {
		warnx("unable to create %s pad window (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->ly, w->lx, w->h, w->w);
		return false;
	}

	if (w->h < st->content_h)
		w->attrs |= ATTR_CAN_FOCUS;

	return true;
}

static void pad_vbox_refresh(struct widget *w)
{
	struct widget_pad_vbox *st = w->state.pad_vbox;
	int ay, ax;

	if (!widget_coordinates_yx(w->parent, &ay, &ax)) {
		warnx("unable to get scrollbar coordinates");
		return;
	}

	pnoutrefresh(w->win, st->scroll, 0,
			ay + w->ly,
			ax + w->lx,
			ay + w->ly + w->h - 1,
			ax + w->lx + w->w - 1);
}

static int pad_vbox_input(const struct widget *w, wchar_t key)
{
	struct widget_pad_vbox *st = w->state.pad_vbox;

	if (w->h >= st->content_h)
		return 0;

	int page = w->h / 2;

	switch (key) {
		case KEY_UP:    st->scroll--; break;
		case KEY_DOWN:  st->scroll++; break;
		case KEY_PPAGE: st->scroll -= page; break;
		case KEY_NPAGE: st->scroll += page; break;
		default:
			return 0;
	}

	int max = st->content_h - w->h;
	st->scroll = CLAMP(st->scroll, 0, max);

	return 1;
}

static void pad_vbox_ensure_visible(struct widget *container, struct widget *child)
{
	struct widget_pad_vbox *st = container->state.pad_vbox;

	int top = st->scroll;
	int bot = st->scroll + container->h;

	if (child->ly < top)
		st->scroll = child->ly;
	else if (child->ly + child->h > bot)
		st->scroll = child->ly + child->h - container->h;

	int max = st->content_h - container->h;
	st->scroll = CLAMP(st->scroll, 0, max);
}

static void pad_vbox_free(struct widget *w)
{
	free(w->state.pad_vbox);
	w->state.pad_vbox = NULL;
}

struct widget *make_pad_vbox(void)
{
	struct widget *w = widget_create(WIDGET_PAD_VBOX);

	struct widget_pad_vbox *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_pad_vbox: calloc");
		widget_free(w);
		return NULL;
	}

	w->state.pad_vbox = st;

	w->measure        = pad_vbox_measure;
	w->layout         = pad_vbox_layout;
	w->create_win     = pad_vbox_createwin;
	w->noutrefresh    = pad_vbox_refresh;
	w->input          = pad_vbox_input;
	w->ensure_visible = pad_vbox_ensure_visible;
	w->free_data      = pad_vbox_free;

	w->stretch_w = true;
	w->stretch_h = true;

	return w;
}
