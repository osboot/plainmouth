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

static void vscroll_measure(struct widget *w)
{
	w->min_w  = 1;
	w->pref_w = 1;
	w->min_h  = 1;
}

static void vscroll_render(struct widget *w)
{
	struct widget_vscroll *s = w->state.vscroll;

	if (s->content <= s->viewport)
		return;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;

	widget_draw_vscroll(w->win, color, s->offset, s->content);
}

static int vscroll_input(const struct widget *w, wchar_t key)
{
	struct widget_vscroll *s = w->state.vscroll;

	if (s->content <= s->viewport)
		return 0;

	int page = MAX(1, s->viewport / 2);

	switch (key) {
		case KEY_UP:
			s->offset--;
			break;
		case KEY_DOWN:
			s->offset++;
			break;
		case KEY_PPAGE:
			s->offset -= page;
			break;
		case KEY_NPAGE:
			s->offset += page;
			break;
		default:
			return 0;
	}

	int max_scroll = s->content - s->viewport;

	if (s->offset < 0)
		s->offset = 0;
	else if (s->offset > max_scroll)
		s->offset = max_scroll;

	return 1;
}

static bool vscroll_setter(struct widget *w, enum widget_property prop, const void *in)
{
	struct widget_vscroll *s = w->state.vscroll;

	switch (prop) {
	case PROP_SCROLL_CONTENT:
		s->content = *(const int *)in;
		return true;
	case PROP_SCROLL_VIEWPORT:
		s->viewport = *(const int *)in;
		return true;
	case PROP_SCROLL_OFFSET:
		s->offset = *(const int *)in;
		return true;
	default:
		return false;
	}
}

static bool vscroll_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_vscroll *s = w->state.vscroll;

	if (prop == PROP_SCROLL_OFFSET) {
		*(int *)out = s->offset;
		return true;
	}
	return false;
}

static void vscroll_free(struct widget *w)
{
	free(w->state.vscroll);
	w->state.vscroll = NULL;
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
	w->input   = vscroll_input;
	w->setter  = vscroll_setter;
	w->getter  = vscroll_getter;
	w->free_data = vscroll_free;

	w->color_pair = COLOR_PAIR_WINDOW;
	w->attrs = ATTR_CAN_FOCUS;

	w->stretch_h = true;
	w->flex_h = 1;
	w->flex_w = 0;

	return w;
}
