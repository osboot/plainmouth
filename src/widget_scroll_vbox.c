// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_svbox {
	struct widget *pad;
	struct widget *vscroll;
};

static void scroll_vbox_sync(struct widget *sv)
{
	struct widget_svbox *st = sv->state.svbox;

	if (!st || !st->pad || !st->vscroll)
		return;

	int scroll_y, content_h;

	widget_get(st->pad, PROP_SCROLL_Y, &scroll_y);
	widget_get(st->pad, PROP_SCROLL_CONTENT_H, &content_h);

	widget_set(st->vscroll, PROP_SCROLL_Y,  &scroll_y);
	widget_set(st->vscroll, PROP_SCROLL_CONTENT_H, &content_h);
	widget_set(st->vscroll, PROP_SCROLL_VIEW_H, &st->pad->h);
}

static void scroll_vbox_measure(struct widget *w)
{
	struct widget_svbox *st = w->state.svbox;
	struct widget *pad = st->pad;

	if (!pad || !pad->measure)
		return;

	pad->measure(pad);

	w->min_h  = pad->min_h;
	w->pref_h = pad->pref_h;

	w->min_w  = pad->min_w + 1;
	w->pref_w = pad->pref_w + 1;
}

static void scroll_vbox_layout(struct widget *w)
{
	struct widget *hbox = TAILQ_FIRST(&w->children);

	if (!hbox)
		return;

	widget_layout_tree(hbox, 0, 0, w->w, w->h);
}

static void scroll_vbox_render(struct widget *w)
{
	//struct widget *hbox = TAILQ_FIRST(&w->children);
	//widget_render_tree(hbox);
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

	int delta = 0;

	switch (key) {
		case KEY_UP:    delta = -1;    break;
		case KEY_DOWN:  delta = +1;    break;
		case KEY_PPAGE: delta = -w->h; break;
		case KEY_NPAGE: delta = +w->h; break;
		default:
				return 0;
	}

	widget_set(st->pad, PROP_SCROLL_INC_Y, &delta);

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

	struct widget *hbox = make_hbox();
	struct widget *pad  = make_pad_box();
	struct widget *vs   = make_vscroll();

	if (!hbox || !pad || !vs) {
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

	root->state.svbox = st;

	widget_add(root, hbox);
	widget_add(hbox, pad);
	widget_add(hbox, vs);

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
