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

static void widget_draw_hscroll(WINDOW *scrollwin, enum color_pair color, int scroll_pos, int content_width)
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

static void hscroll_measure(struct widget *w)
{
	w->min_h = w->max_h = w->pref_h = 1;
	w->min_w = 1;
}

static void hscroll_render(struct widget *w)
{
	struct widget_hscroll *s = w->state.hscroll;

	warnx("XXX hscroll_render content=%d viewport=%d", s->content, s->viewport);

	if (s->content <= s->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	widget_draw_hscroll(w->win, color, s->offset, s->content);
}

static bool hscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_hscroll *s = w->state.hscroll;

	switch (prop) {
	case PROP_SCROLL_CONTENT_W:
		s->content = *(const int *)in;
		return true;
	case PROP_SCROLL_VIEW_W:
		s->viewport = *(const int *)in;
		return true;
	case PROP_SCROLL_X:
		s->offset = *(const int *)in;
		return true;
	default:
		return false;
	}
}

static bool hscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_hscroll *s = w->state.hscroll;

	if (prop == PROP_SCROLL_X) {
		*(int *)out = s->offset;
		return true;
	}
	return false;
}

static void hscroll_free(struct widget *w)
{
	free(w->state.hscroll);
	w->state.hscroll = NULL;
}

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

	w->state.hscroll = s;

	w->measure = hscroll_measure;
	w->render  = hscroll_render;
	w->setter  = hscroll_setter;
	w->getter  = hscroll_getter;
	w->free_data = hscroll_free;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 0;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
