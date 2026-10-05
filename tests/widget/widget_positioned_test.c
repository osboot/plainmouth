// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <limits.h>

#include "widget.h"

int main(void)
{
	struct widget *canvas = make_positioned();
	struct widget *first = make_input(L"one", NULL);
	struct widget *second = make_label(L"two");
	assert(canvas && first && second);
	assert(!positioned_add(canvas, first, -1, 0, 4, 1));
	assert(!positioned_add(canvas, first, INT_MAX, 0, 4, 1));
	assert(!positioned_add(canvas, first, 0, 0, 0, 1));
	assert(!first->parent);
	assert(positioned_add(canvas, first, 20, 12, 4, 1));
	assert(!positioned_add(canvas, first, 0, 0, 4, 1));
	assert(positioned_add(canvas, second, 1, 2, 8, 1));
	assert(TAILQ_FIRST(&canvas->children) == first);
	widget_measure_tree(canvas);
	assert(canvas->pref_w == 24 && canvas->pref_h == 13);
	widget_layout_tree(canvas, 0, 0, 24, 13);
	assert(first->lx == 20 && first->ly == 12 && first->w == 4 && first->h == 1);
	widget_measure_tree(canvas);
	widget_layout_tree(canvas, 0, 0, 40, 30);
	assert(first->lx == 20 && first->ly == 12 && first->w == 4);
	assert(second->lx == 1 && second->ly == 2 && second->w == 8);
	struct widget *pad = make_pad_box();
	assert(pad);
	widget_add(pad, canvas);
	widget_measure_tree(pad);
	widget_layout_tree(pad, 0, 0, 10, 5);
	pad->ops->ensure_visible(pad, first);
	int x, y;
	assert(widget_get(pad, PROP_SCROLL_X, &x));
	assert(widget_get(pad, PROP_SCROLL_Y, &y));
	assert(x == 14 && y == 8);
	pad->ops->ensure_visible(pad, second);
	assert(widget_get(pad, PROP_SCROLL_X, &x));
	assert(widget_get(pad, PROP_SCROLL_Y, &y));
	assert(x == 1 && y == 2);
	widget_free(pad);
	return 0;
}
