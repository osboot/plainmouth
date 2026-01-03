// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_svbox {
	WINDOW *scrollwin;
	int content_h, content_w;
	int scroll;
};

static void scroll_vbox_measure(struct widget *w)
{
	struct widget *c;

	TAILQ_FOREACH(c, &w->children, siblings) {
		w->min_h  += c->min_h;
		w->pref_h += c->pref_h;

		w->min_w  = MAX(w->min_w,  c->min_w);
		w->pref_w = MAX(w->pref_w, c->pref_w);
	}

	w->min_h  = MIN(w->min_h, 1);
}

static void scroll_vbox_layout(struct widget *w)
{
	struct widget *c;
	struct widget_svbox *state = w->state.svbox;

	state->content_h = 0;
	state->content_w = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : c->min_w;

		state->content_h += ch;
		state->content_w = MAX(state->content_w, cw);
	}

	int max_scroll = state->content_h - w->h;
	if (max_scroll < 0)
		max_scroll = 0;

	if (state->scroll > max_scroll)
		state->scroll = max_scroll;
	if (state->scroll < 0)
		state->scroll = 0;

	if (max_scroll)
		state->content_w -= 1;

	int y = 0;

	TAILQ_FOREACH(c, &w->children, siblings) {
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		int cw = c->stretch_w ? w->w : c->min_w;

		widget_layout_tree(c, 0, y, cw - (max_scroll ? 1 : 0), ch);
		y += ch;
	}
}

static bool scroll_vbox_createwin(struct widget *w)
{
	struct widget_svbox *state = w->state.svbox;

	w->win = newpad(state->content_h, state->content_w);
	if (!w->win) {
		warnx("unable to create %s pad window (y=%d, x=%d, height=%d, width=%d)",
			widget_type(w), w->ly, w->lx, w->h, w->w);
		return false;
	}

	if (w->h < state->content_h) {
		int ay, ax;

		if (!widget_coordinates_yx(w->parent, &ay, &ax)) {
			warnx("unable to get scrollbar coordinates");
			return false;
		}

		ay += w->ly;
		ax += w->lx + w->w - 1;

		state->scrollwin = newwin(w->h, 1, ay, ax);
		if (!state->scrollwin) {
			warnx("unable to create scrollbar window");
			return false;
		}

		w->attrs |= ATTR_CAN_FOCUS;
	}

	return true;
}

static void scroll_vbox_render(struct widget *w)
{
	struct widget_svbox *state = w->state.svbox;

	werase(w->win);
	wbkgd(w->win, COLOR_PAIR(w->color_pair));

	if (state->scrollwin) {
		widget_draw_vscroll(state->scrollwin, COLOR_PAIR_FOCUS, state->scroll, state->content_h);
		wnoutrefresh(state->scrollwin);
	}
}

static void scroll_vbox_refresh(struct widget *w)
{
	struct widget_svbox *state = w->state.svbox;
	int ay, ax;

	if (!widget_coordinates_yx(w->parent, &ay, &ax))
		return;

	ay += w->ly;
	ax += w->lx;

	pnoutrefresh(w->win, state->scroll, 0, ay, ax,
			ay + w->h - 1,
			ax + w->w - 1);
}

static int scroll_vbox_input(const struct widget *w, wchar_t key)
{
	struct widget_svbox *state = w->state.svbox;

	if (w->h >= state->content_h)
		return 0;

	int page = w->h / 2;

	switch (key) {
		case KEY_UP:
			state->scroll--;
			break;
		case KEY_DOWN:
			state->scroll++;
			break;
		case KEY_PPAGE:
			state->scroll -= page;
			break;
		case KEY_NPAGE:
			state->scroll += page;
			break;
		default:
			return 0;
	}

	if ((state->scroll + w->h) > state->content_h)
		state->scroll = state->content_h - w->h;
	else if (state->scroll < 0)
		state->scroll = 0;

	return 1;
}

static void scroll_vbox_freedata(struct widget *w)
{
	struct widget_svbox *state = w->state.svbox;

	if (state->scrollwin)
		delwin(state->scrollwin);

	free(w->state.svbox);
	w->state.svbox = NULL;
}

struct widget *make_scroll_vbox(void)
{
	struct widget *w = widget_create(WIDGET_SCROLL_VBOX);

	struct widget_svbox *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_scroll_vbox: calloc");
		widget_free(w);
		return NULL;
	}

	w->state.svbox = state;
	w->measure     = scroll_vbox_measure;
	w->layout      = scroll_vbox_layout;
	w->render      = scroll_vbox_render;
	w->create_win  = scroll_vbox_createwin;
	w->noutrefresh = scroll_vbox_refresh;
	w->input       = scroll_vbox_input;
	w->free_data   = scroll_vbox_freedata;
	w->color_pair  = COLOR_PAIR_WINDOW;

	w->stretch_w = true;
	w->stretch_h = true;

	return w;
}
