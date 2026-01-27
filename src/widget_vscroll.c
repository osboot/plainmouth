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
	struct widget_vscroll *s = w->state.vscroll;

	if (s->content <= s->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	widget_draw_vscroll(w->win, color, s->offset, s->content);
}

bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_vscroll *s = w->state.vscroll;

	switch (prop) {
	case PROP_SCROLL_CONTENT_H:
		s->content = *(const int *)in;
		return true;
	case PROP_SCROLL_VIEW_H:
		s->viewport = *(const int *)in;
		return true;
	case PROP_SCROLL_Y:
		s->offset = *(const int *)in;
		return true;
	default:
		return false;
	}
}

bool vscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_vscroll *s = w->state.vscroll;

	if (prop == PROP_SCROLL_Y) {
		*(int *)out = s->offset;
		return true;
	}
	return false;
}

void vscroll_free(struct widget *w)
{
	if (!w)
		return;

	if (w->state.vscroll) {
		free(w->state.vscroll);
		w->state.vscroll = NULL;
	}
}

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

	w->state.vscroll = s;

	w->measure = vscroll_measure;
	w->render  = vscroll_render;
	w->setter  = vscroll_setter;
	w->getter  = vscroll_getter;
	w->free_data = vscroll_free;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_h = 1;
	w->stretch_w = 1;

	w->flex_h = 0;
	w->flex_w = 0;

	return w;
}
