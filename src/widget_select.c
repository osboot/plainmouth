// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_select {
	int max_selected;
	int selected;
	struct widget *focus;
	struct widget *list;
	struct widget *vscroll;
};

static void select_sync(struct widget *w) __attribute__((nonnull(1)));
static void select_measure(struct widget *w) __attribute__((nonnull(1)));
static void select_layout(struct widget *w) __attribute__((nonnull(1)));
static void select_render(struct widget *w) __attribute__((nonnull(1)));
static void select_ensure_visible(struct widget *w, struct widget *child) __attribute__((nonnull(1,2)));
static int select_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool select_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1,3)));
static bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *value) __attribute__((nonnull(1,4)));
static void select_add_child(struct widget *sv, struct widget *child) __attribute__((nonnull(1,2)));
static void select_free(struct widget *w);


void select_sync(struct widget *sv)
{
	struct widget_select *st = sv->state;

	int scroll_y, content_h;

	widget_get(st->list, PROP_SCROLL_Y, &scroll_y);
	widget_get(st->list, PROP_SCROLL_CONTENT_H, &content_h);

	widget_set(st->vscroll, PROP_SCROLL_Y,  &scroll_y);
	widget_set(st->vscroll, PROP_SCROLL_CONTENT_H, &content_h);
	widget_set(st->vscroll, PROP_SCROLL_VIEW_H, &st->list->h);

	widget_render_tree(st->vscroll);
}

void select_measure(struct widget *w)
{
	struct widget_select *st = w->state;

	st->list->ops->measure(st->list);

	w->min_h  = 1;
	w->pref_h = st->list->pref_h;

	w->min_w  = 1;
	w->pref_w = st->list->pref_w + 1;
}

void select_layout(struct widget *w)
{
	struct widget *hbox = TAILQ_FIRST(&w->children);

	widget_layout_tree(hbox, 0, 0, w->w, w->h);
}

void select_render(struct widget *w)
{
	struct widget_select *st = w->state;

	if (w->flags & FLAG_INFOCUS)
		st->vscroll->flags |= FLAG_INFOCUS;
	else
		st->vscroll->flags &= ~FLAG_INFOCUS;

	select_sync(w);
}

void select_ensure_visible(struct widget *w, struct widget *child)
{
	struct widget_select *st = w->state;

	st->list->ops->ensure_visible(st->list, child);

	select_sync(w);
}

int select_input(const struct widget *w, wchar_t key)
{
	struct widget_select *st = w->state;
	int delta_y = 0;

	switch (key) {
		case L' ':
			if (st->focus) {
				bool clicked = false;

				widget_get(st->focus, PROP_CHECKBOX_STATE, &clicked);

				if (clicked)
					st->selected--;
				else if (st->selected < st->max_selected)
					st->selected++;
				else
					return 1;

				clicked = !clicked;
				widget_set(st->focus, PROP_CHECKBOX_STATE, &clicked);
			}
			break;

		case KEY_UP:
			st->focus->flags &= ~FLAG_INFOCUS;
			st->focus = TAILQ_PREV(st->focus, widgethead, siblings);

			if (!st->focus)
				st->focus = TAILQ_FIRST(&st->list->children);

			st->focus->flags |= FLAG_INFOCUS;
			st->list->ops->ensure_visible(st->list, st->focus);
			break;

		case KEY_DOWN:
			st->focus->flags &= ~FLAG_INFOCUS;
			st->focus = TAILQ_NEXT(st->focus, siblings);

			if (!st->focus)
				st->focus = TAILQ_FIRST(&st->list->children);

			st->focus->flags |= FLAG_INFOCUS;
			st->list->ops->ensure_visible(st->list, st->focus);
			break;

		case KEY_PPAGE:
			delta_y = -w->h;
			break;

		case KEY_NPAGE:
			delta_y = +w->h;
			break;

		default:
			return 0;
	}
	if (delta_y)
		widget_set(st->list, PROP_SCROLL_INC_Y, &delta_y);

	return 1;
}

bool select_getter(struct widget *w, enum widget_property prop, void *value)
{
	struct widget_select *st = w->state;

	if (prop == PROP_SELECT_OPTIONS_SIZE) {
		int size = 0;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings) {
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			size++;
		}
		*(int *) value = size;
		return true;
	}

	if (prop == PROP_SELECT_CURSOR) {
		int index = 0;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings) {
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			if (c == st->focus)
				break;

			index++;
		}
		*(int *) value = index;
		return true;
	}

	return false;
}

bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *value)
{
	struct widget_select *st = w->state;

	if (prop == PROP_SELECT_OPTION_VALUE) {
		int i = 0;
		bool clicked = false;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings) {
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			if (i == index) {
				widget_get(c, PROP_CHECKBOX_STATE, &clicked);
				break;
			}
			i++;
		}

		*(bool *) value = clicked;
		return true;
	}

	return false;
}

void select_add_child(struct widget *sv, struct widget *child)
{
	struct widget_select *st = sv->state;

	if (!st->focus) {
		st->focus = child;
		child->flags |= FLAG_INFOCUS;
	}

	child->attrs &= ~ATTR_CAN_FOCUS;
	widget_add(st->list, child);
}

void select_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

static const struct widget_ops select_ops = {
	.measure          = select_measure,
	.layout           = select_layout,
	.render           = select_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = select_free,
	.input            = select_input,
	.add_child        = select_add_child,
	.ensure_visible   = select_ensure_visible,
	.setter           = NULL,
	.getter           = select_getter,
	.getter_index     = select_getter_index,
};

struct widget *make_select(int max_selected, int view_rows)
{
	struct widget *root = widget_create(WIDGET_SELECT);
	struct widget *hbox = make_hbox();
	struct widget *list = make_list_vbox(view_rows);
	struct widget *vs   = make_vscroll();

	if (!root || !hbox || !list || !vs) {
		goto fail;
	}

	struct widget_select *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_select_box: calloc");
		goto fail;
	}

	st->max_selected = max_selected;
	st->list         = list;
	st->vscroll      = vs;

	root->state = st;

	widget_add(root, hbox);
	widget_add(hbox, list);
	widget_add(hbox, vs);

	root->ops = &select_ops;
	root->color_pair     = COLOR_PAIR_WINDOW;
	root->attrs          = ATTR_CAN_FOCUS;

	root->stretch_w = true;
	root->stretch_h = true;

	root->flex_w = 1;
	root->flex_h = 1;

	return root;
fail:
	widget_free(list);
	widget_free(vs);
	widget_free(hbox);
	widget_free(root);

	return NULL;
}
