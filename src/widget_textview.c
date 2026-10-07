// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include "widget.h"

static void textview_measure(struct widget *w)
{
	struct widget *scroll = TAILQ_FIRST(&w->children);
	w->min_w = scroll->min_w;
	w->min_h = scroll->min_h;
	w->pref_w = scroll->pref_w;
	w->pref_h = scroll->pref_h;
}

static void textview_layout(struct widget *w)
{
	widget_layout_tree(TAILQ_FIRST(&w->children), 0, 0, w->w, w->h);
}

static bool textview_set(struct widget *w, enum widget_property prop, const void *value)
{
	if (prop != PROP_TEXT_VALUE)
		return false;

	struct widget *label = find_widget_by_type_and_id(w, WIDGET_LABEL, 0);
	struct widget *pad = find_widget_by_type_and_id(w, WIDGET_PAD_BOX, 0);

	if (!widget_set(label, prop, value))
		return false;

	label->pref_w = label->pref_h = 0;
	int start = 0;
	widget_set(pad, PROP_SCROLL_X, &start);
	widget_set(pad, PROP_SCROLL_Y, &start);
	return true;
}

static const struct widget_ops textview_ops = {
	.measure = textview_measure,
	.layout = textview_layout,
	.setter = textview_set,
};

struct widget *make_textview(const wchar_t *text)
{
	struct widget *root = widget_create(WIDGET_TEXTVIEW);
	struct widget *scroll = make_scroll_vbox();
	struct widget *w = make_label(text);

	if (!root || !scroll || !w) {
		widget_free(root);
		widget_free(scroll);
		widget_free(w);
		return NULL;
	}

	widget_add(scroll, w);
	widget_add(root, scroll);
	root->ops = &textview_ops;
	root->color_pair = COLOR_PAIR_WINDOW;
	root->flex_w = root->flex_h = 1;
	root->stretch_w = root->stretch_h = true;

	return root;
}
