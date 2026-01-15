// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_svbox {
	struct widget *pad;
	struct widget *vscroll;
	struct widget *hscroll;
};

static void scroll_vbox_sync(struct widget *sv)
{
	struct widget_svbox *st = sv->state.svbox;

	if (!st || !st->pad)
		return;

	if (st->vscroll) {
		int scroll_y, content_h;

		widget_get(st->pad, PROP_SCROLL_Y, &scroll_y);
		widget_get(st->pad, PROP_SCROLL_CONTENT_H, &content_h);

		widget_set(st->vscroll, PROP_SCROLL_Y,  &scroll_y);
		widget_set(st->vscroll, PROP_SCROLL_CONTENT_H, &content_h);
		widget_set(st->vscroll, PROP_SCROLL_VIEW_H, &st->pad->h);
	}

	if (st->hscroll) {
		int scroll_x, content_w;

		widget_get(st->pad, PROP_SCROLL_X, &scroll_x);
		widget_get(st->pad, PROP_SCROLL_CONTENT_W, &content_w);

		widget_set(st->hscroll, PROP_SCROLL_X,  &scroll_x);
		widget_set(st->hscroll, PROP_SCROLL_CONTENT_W, &content_w);
		widget_set(st->hscroll, PROP_SCROLL_VIEW_W, &st->pad->w);
	}
}

static void scroll_vbox_measure(struct widget *w)
{
	struct widget_svbox *st = w->state.svbox;
	struct widget *pad = st->pad;

	if (!pad || !pad->measure)
		return;

	pad->measure(pad);

	/*
	 * scroll_vbox reserves space for scrollbars in measure(),
	 * but may give this space back to pad in layout() if scrolling
	 * is not required.
	 */
	w->min_h  = 1;
	w->pref_h = pad->pref_h + 1;

	w->min_w  = 1;
	w->pref_w = pad->pref_w + 1;
}

static void scroll_vbox_layout(struct widget *w)
{
	struct widget_svbox *st = w->state.svbox;
	struct widget *vbox = TAILQ_FIRST(&w->children);
	if (!vbox)
		return;

	widget_layout_tree(vbox, 0, 0, w->w, w->h);

	int content_h = 0, content_w = 0;
	widget_get(st->pad, PROP_SCROLL_CONTENT_H, &content_h);
	widget_get(st->pad, PROP_SCROLL_CONTENT_W, &content_w);

	/*
	 * We check whether the content fits into the widget without taking
	 * scrollbars into account. If the content is larger than pad but not
	 * larger than the size of scrollbar, then the scrollbar is not needed.
	 */
	bool need_vscroll = (content_h > w->h);
	bool need_hscroll = (content_w > w->w);

	bool relayout = false;

	if (!need_vscroll && st->vscroll->min_w > 0) {
		st->vscroll->min_h  = st->vscroll->min_w  = 0;
		st->vscroll->pref_h = st->vscroll->pref_w = 0;
		relayout = true;
	}

	if (!need_hscroll && st->hscroll->min_h > 0) {
		st->hscroll->min_h  = st->hscroll->min_w  = 0;
		st->hscroll->pref_h = st->hscroll->pref_w = 0;
		relayout = true;
	}

	if (relayout) {
		widget_layout_tree(vbox, 0, 0, w->w, w->h);
	}
}

static void scroll_vbox_render(struct widget *w)
{
	struct widget *hbox = TAILQ_FIRST(&w->children);
	if (hbox)
		widget_render_tree(hbox);
	scroll_vbox_sync(w);
}

static void scroll_vbox_ensure_visible(struct widget *w,
                                       struct widget *child)
{
	struct widget_svbox *st = w->state.svbox;

	if (!st || !st->pad)
		return;

	st->pad->ensure_visible(st->pad, child);
	scroll_vbox_sync(w);
}

static void scroll_vbox_add_child(struct widget *sv, struct widget *child)
{
	struct widget_svbox *st = sv->state.svbox;

	if (!st || !st->pad)
		return;

	widget_add(st->pad, child);
}

static int scroll_vbox_input(const struct widget *w, wchar_t key)
{
	struct widget_svbox *st = w->state.svbox;

	int delta_y = 0;
	int delta_x = 0;

	switch (key) {
		case KEY_UP:    delta_y = -1;    break;
		case KEY_DOWN:  delta_y = +1;    break;
		case KEY_PPAGE: delta_y = -w->h; break;
		case KEY_NPAGE: delta_y = +w->h; break;
		case KEY_LEFT:  delta_x = -1;    break;
		case KEY_RIGHT: delta_x = +1;    break;
		default:
				return 0;
	}

	if (delta_y)
		widget_set(st->pad, PROP_SCROLL_INC_Y, &delta_y);

	if (delta_x)
		widget_set(st->pad, PROP_SCROLL_INC_X, &delta_x);

	return 1;
}

static void scroll_vbox_free(struct widget *w)
{
	if (!w || !w->state.svbox)
		return;

	free(w->state.svbox);
	w->state.svbox = NULL;
}

struct widget *make_scroll_vbox(void)
{
	struct widget *root = widget_create(WIDGET_SCROLL_VBOX);
	if (!root)
		return NULL;

	struct widget *vbox = make_vbox();
	struct widget *hbox = make_hbox();
	struct widget *pad  = make_pad_box();
	struct widget *vs   = make_vscroll();
	struct widget *hs   = make_hscroll();

	if (!hbox || !pad || !vs || !hs) {
		widget_free(root);
		return NULL;
	}

	struct widget_svbox *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_scroll_vbox: calloc");
		widget_free(root);
		return NULL;
	}

	st->pad     = pad;
	st->vscroll = vs;
	st->hscroll = hs;

	root->state.svbox = st;

	widget_add(root, vbox);
	widget_add(vbox, hbox);
	widget_add(hbox, pad);
	widget_add(hbox, vs);
	widget_add(vbox, hs);

	root->add_child      = scroll_vbox_add_child;
	root->measure        = scroll_vbox_measure;
	root->layout         = scroll_vbox_layout;
	root->render         = scroll_vbox_render;
	root->ensure_visible = scroll_vbox_ensure_visible;
	root->input          = scroll_vbox_input;
	root->free_data      = scroll_vbox_free;
	root->color_pair     = COLOR_PAIR_WINDOW;
	root->attrs          = ATTR_CAN_FOCUS;

	root->stretch_w = true;
	root->stretch_h = true;

	root->flex_w = 1;
	root->flex_h = 1;

	return root;
}
