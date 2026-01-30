// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_vscroll {
	int content;   /* total size */
	int viewport;  /* visible size */
	int offset;    /* current scroll */
};

static void widget_draw_vscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_height) __attribute__((nonnull(1)));
static void vscroll_measure(struct widget *w) __attribute__((nonnull(1)));
static void vscroll_render(struct widget *w) __attribute__((nonnull(1)));
static bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static bool vscroll_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static void vscroll_free(struct widget *w);


void widget_draw_vscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_height)
{
	int view_width, view_height;
	getmaxyx(scrollwin, view_height, view_width);

	if ((content_height - view_height) <= 0)
		return;

	int thumb_size = MAX(1, (view_height * view_height) / content_height);
	int thumb_pos = (scroll_pos * (view_height - thumb_size)) / (content_height - view_height);

	wattron(scrollwin, COLOR_PAIR(color) | A_NORMAL);
	for (int i = 0; i < view_height; i++)
		mvwaddch(scrollwin, i, view_width - 1, ACS_CKBOARD);
	wattroff(scrollwin, COLOR_PAIR(color) | A_NORMAL);

	wattron(scrollwin, COLOR_PAIR(color) | A_REVERSE);
	for (int i = 0; i < thumb_size; i++) {
		chtype c = ' ';

		if (thumb_size >= 2) {
			if (i == 0)
				c = '^';
			else if (i == thumb_size - 1)
				c = 'v';
		}

		mvwaddch(scrollwin, thumb_pos + i, view_width - 1, c);
	}
	wattroff(scrollwin, COLOR_PAIR(color) | A_REVERSE);
}

void vscroll_measure(struct widget *w)
{
	w->min_w = w->max_w = w->pref_w = 1;
	w->min_h = 1;
}

void vscroll_render(struct widget *w)
{
	struct widget_vscroll *st = w->state;

	if (st->content <= st->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	widget_draw_vscroll(w->win, color, st->offset, st->content);
}

bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_vscroll *st = w->state;

	switch (prop) {
	case PROP_SCROLL_CONTENT_H:
		st->content = *(const int *)in;
		return true;
	case PROP_SCROLL_VIEW_H:
		st->viewport = *(const int *)in;
		return true;
	case PROP_SCROLL_Y:
		st->offset = *(const int *)in;
		return true;
	default:
		return false;
	}
}

bool vscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_vscroll *st = w->state;

	if (prop == PROP_SCROLL_Y) {
		*(int *)out = st->offset;
		return true;
	}
	return false;
}

void vscroll_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

static const struct widget_ops vscroll_ops = {
	.measure          = vscroll_measure,
	.layout           = NULL,
	.render           = vscroll_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free_data        = vscroll_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = vscroll_setter,
	.getter           = vscroll_getter,
	.getter_index     = NULL,
};

struct widget *make_vscroll(void)
{
	struct widget *w = widget_create(WIDGET_VSCROLL);
	if (!w)
		return NULL;

	struct widget_vscroll *s = calloc(1, sizeof(*s));
	if (!s) {
		warn("make_vscroll: calloc");
		widget_free(w);
		return NULL;
	}

	w->state = s;

	w->ops = &vscroll_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 1;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
