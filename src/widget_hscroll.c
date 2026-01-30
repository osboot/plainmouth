// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_hscroll {
	int content;   /* total size */
	int viewport;  /* visible size */
	int offset;    /* current scroll */
};

static void widget_draw_hscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_width) __attribute__((nonnull(1)));
static void hscroll_measure(struct widget *w) __attribute__((nonnull(1)));
static void hscroll_render(struct widget *w) __attribute__((nonnull(1)));
static bool hscroll_setter(struct widget *w, enum widget_property prop, const void *in) __attribute__((nonnull(1,3)));
static bool hscroll_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static void hscroll_free(struct widget *w);

void widget_draw_hscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_width)
{
	int view_width, view_height;
	getmaxyx(scrollwin, view_height, view_width);

	if ((content_width - view_width) <= 0)
		return;

	int thumb_size = MAX(1, (view_width * view_width) / content_width);
	int thumb_pos = (scroll_pos * (view_width - thumb_size)) / (content_width - view_width);

	wattron(scrollwin, COLOR_PAIR(color) | A_NORMAL);
	for (int i = 0; i < view_width; i++)
		mvwaddch(scrollwin, view_height - 1, i, ACS_CKBOARD);
	wattroff(scrollwin, COLOR_PAIR(color) | A_NORMAL);

	wattron(scrollwin, COLOR_PAIR(color) | A_REVERSE);
	for (int i = 0; i < thumb_size; i++) {
		chtype c = ' ';

		if (thumb_size >= 2) {
			if (i == 0)
				c = '<';
			else if (i == thumb_size - 1)
				c = '>';
		}

		mvwaddch(scrollwin, view_height - 1, thumb_pos + i, c);
	}
	wattroff(scrollwin, COLOR_PAIR(color) | A_REVERSE);
}

void hscroll_measure(struct widget *w)
{
	w->min_h = w->max_h = w->pref_h = 1;
	w->min_w = 1;
}

void hscroll_render(struct widget *w)
{
	struct widget_hscroll *st = w->state;

	if (st->content <= st->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	widget_draw_hscroll(w->win, color, st->offset, st->content);
}

bool hscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_hscroll *st = w->state;

	switch (prop) {
	case PROP_SCROLL_CONTENT_W:
		st->content = *(const int *)in;
		return true;
	case PROP_SCROLL_VIEW_W:
		st->viewport = *(const int *)in;
		return true;
	case PROP_SCROLL_X:
		st->offset = *(const int *)in;
		return true;
	default:
		return false;
	}
}

bool hscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_hscroll *st = w->state;

	if (prop == PROP_SCROLL_X) {
		*(int *)out = st->offset;
		return true;
	}
	return false;
}

void hscroll_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

static const struct widget_ops hscroll_ops = {
	.measure          = hscroll_measure,
	.layout           = NULL,
	.render           = hscroll_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free_data        = hscroll_free,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = hscroll_setter,
	.getter           = hscroll_getter,
	.getter_index     = NULL,
};

struct widget *make_hscroll(void)
{
	struct widget *w = widget_create(WIDGET_HSCROLL);
	if (!w)
		return NULL;

	struct widget_hscroll *s = calloc(1, sizeof(*s));
	if (!s) {
		warn("make_hscroll: calloc");
		widget_free(w);
		return NULL;
	}

	w->state   = s;
	w->ops     = &hscroll_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 0;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
