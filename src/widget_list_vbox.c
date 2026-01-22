// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <stdbool.h>
#include <stdlib.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_list_vbox {
	int view_rows;
	int scroll_y;
	int content_h;
};

static inline int widget_height(const struct widget *c)
{
	return MAX(1, c->pref_h ? c->pref_h : c->min_h);
}

static void list_vbox_measure(struct widget *w)
{
	struct widget *c;
	int max_w = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		max_w = MAX(max_w, c->pref_w ? c->pref_w : c->min_w);
	}

	w->min_w = max_w;
	w->min_h = 1;
	w->pref_w = max_w;
	w->pref_h = w->state.list_vbox->view_rows ?: 5;
}

static void list_vbox_layout(struct widget *w)
{
	int y = 0;

	w->state.list_vbox->content_h = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = widget_height(c);

		widget_layout_tree(c, 0, y, w->w, ch);
		y += ch;

		w->state.list_vbox->content_h += ch;
	}
}

static void list_vbox_render(struct widget *w)
{
	werase(w->win);
	wbkgd(w->win, COLOR_PAIR(w->color_pair));

	int y = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = widget_height(c);

		if (!(c->flags & FLAG_VISIBLE) || (y + ch) > w->h) {
			widget_hide_tree(c);
			c->flags &= ~FLAG_VISIBLE;
			continue;
		}
		widget_layout_tree(c, 0, y, w->w, ch);
		y += ch;
	}
}

static void get_visible_range(struct widget *w, struct widget **first, struct widget **last)
{
	*first = *last = NULL;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		if (!(c->flags & FLAG_VISIBLE))
			continue;
		if (!*first)
			*first = c;
		*last = c;
	}
}

static void set_visible_range(struct widget *w, struct widget *first, struct widget *last)
{
	struct widget_list_vbox *st = w->state.list_vbox;
	bool first_found = false;

	st->scroll_y = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		if (!first_found) {
			if (c == first)
				first_found = true;
			st->scroll_y += c->h;
		}
		c->flags &= ~FLAG_VISIBLE;
	}
	for (c = first; c && c != TAILQ_NEXT(last, siblings); c = TAILQ_NEXT(c, siblings)) {
		c->flags |= FLAG_VISIBLE;
	}
}

static void shift_window_anchor_first(struct widget *w, struct widget *focused)
{
	struct widget *c, *first, *last;

	int total = 0;
	first = last = focused;

	for (c = first; c; c = TAILQ_NEXT(c, siblings)) {
		int h = widget_height(c);

		if ((total + h) > w->h)
			break;
		total += h;
		last = c;
	}
	set_visible_range(w, first, last);
}

static void shift_window_anchor_last(struct widget *w, struct widget *focused)
{
	struct widget *c, *first, *last;

	first = last = NULL;
	get_visible_range(w, &first, &last);

	if (!first || !last)
		return;

	for (c = first; c && c != TAILQ_NEXT(last, siblings); c = TAILQ_NEXT(c, siblings)) {
		if (c == focused)
			return;
	}

	first = last = focused;

	int total = 0;
	for (c = last; c; c = TAILQ_PREV(c, widgethead, siblings)) {
		int h = widget_height(c);
		if ((total + h) > w->h)
			break;
		total += h;
		first = c;
	}

	set_visible_range(w, first, last);
}

static void list_vbox_ensure_visible(struct widget *w, struct widget *focused)
{
	struct widget *c, *first, *last;

	first = last = NULL;
	get_visible_range(w, &first, &last);

	if (!first || !last)
		return;

	for (c = first; c && c != TAILQ_NEXT(last, siblings); c = TAILQ_NEXT(c, siblings)) {
		if (c == focused)
			return;
	}
	for (c = TAILQ_FIRST(&w->children); c && c != first; c = TAILQ_NEXT(c, siblings)) {
		if (c == focused) {
			shift_window_anchor_first(w, focused);
			return;
		}
	}
	for (c = TAILQ_NEXT(last, siblings); c; c = TAILQ_NEXT(c, siblings)) {
		if (c == focused) {
			shift_window_anchor_last(w, focused);
			return;
		}
	}
}

static bool list_vbox_getter(struct widget *w, enum widget_property prop, void *val)
{
	struct widget_list_vbox *st = w->state.list_vbox;

	switch (prop) {
		case PROP_SCROLL_Y:
			*(int *) val = st->scroll_y;
			return true;
		case PROP_SCROLL_CONTENT_H:
			*(int *) val = st->content_h;
			return true;
		default:
			errx(EXIT_FAILURE, "unknown property: %d", prop);
	}
	return false;
}

static struct widget *list_vbox_find_anchor_by_scroll_y(struct widget *w, int scroll_y)
{
	int acc = 0;
	struct widget *c;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int h = widget_height(c);

		if (acc + h > scroll_y)
			return c;

		acc += h;
	}

	return TAILQ_LAST(&w->children, widgethead);
}


static bool list_vbox_setter(struct widget *w, enum widget_property prop, const void *val)
{
	struct widget_list_vbox *st = w->state.list_vbox;
	int target_y = 0;

	switch (prop) {
		case PROP_SCROLL_Y:
			target_y = *(const int *)val;
			break;
		case PROP_SCROLL_INC_Y:
			target_y = st->scroll_y + *(const int *)val;
			break;
		default:
			return false;
	}

	int max_scroll = MAX(0, st->content_h - w->h);
	target_y = CLAMP(target_y, 0, max_scroll);

	struct widget *anchor = list_vbox_find_anchor_by_scroll_y(w, target_y);
	if (anchor)
		shift_window_anchor_first(w, anchor);

	return true;
}

static void list_vbox_free(struct widget *w)
{
	if (!w || !w->state.list_vbox)
		return;

	free(w->state.list_vbox);
	w->state.list_vbox = NULL;
}

struct widget *make_list_vbox(int view_rows)
{
	struct widget *w = widget_create(WIDGET_LIST_VBOX);
	if (!w)
		return NULL;

	struct widget_list_vbox *s = calloc(1, sizeof(*s));
	if (!s) {
		warn("make_list_vbox: calloc");
		widget_free(w);
		return NULL;
	}

	s->view_rows = view_rows;
	w->state.list_vbox = s;

	w->measure        = list_vbox_measure;
	w->layout         = list_vbox_layout;
	w->render         = list_vbox_render;
	w->ensure_visible = list_vbox_ensure_visible;
	w->getter         = list_vbox_getter;
	w->setter         = list_vbox_setter;
	w->free_data      = list_vbox_free;
	w->color_pair     = COLOR_PAIR_WINDOW;

	w->flex_h = 1;
	w->flex_w = 1;
	w->stretch_w = true;
	w->stretch_h = true;

	return w;
}
