// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <string.h>

#include "widget.h"

static void test_horizontal(void)
{
	struct widget *row = make_hbox();
	struct widget *space = make_spacer(0, 0);
	struct widget *ok = make_button(L"OK");
	struct widget *cancel = make_button(L"Cancel");
	assert(row && space && ok && cancel);
	int gap = 2;
	assert(widget_set(row, PROP_BOX_GAP, &gap));
	space->flex_w = 1;
	widget_add(row, space);
	widget_add(row, ok);
	widget_add(row, cancel);
	widget_measure_tree(row);
	assert(row->min_w == 16 && row->min_h == 1);
	widget_layout_tree(row, 0, 0, 32, 1);
	assert(space->w == 16 && space->h == 1);
	assert(ok->lx == 18 && cancel->lx == 24 && cancel->w == 8);
	widget_measure_tree(row);
	widget_layout_tree(row, 0, 0, 22, 1);
	assert(space->w == 6 && ok->lx == 8 && cancel->lx == 14);
	assert(cancel->lx + cancel->w == row->w);
	assert(!(space->attrs & ATTR_CAN_FOCUS));
	gap = -1;
	assert(!widget_set(row, PROP_BOX_GAP, &gap));
	gap = 4097;
	assert(!widget_set(row, PROP_BOX_GAP, &gap));
	assert(widget_get(row, PROP_BOX_GAP, &gap) && gap == 2);
	widget_free(row);
}

static void test_vertical(void)
{
	struct widget *column = make_vbox();
	struct widget *top = make_label(L"Top");
	struct widget *space = make_spacer(0, 3);
	struct widget *bottom = make_button(L"OK");
	assert(column && top && space && bottom);
	int gap = 2;
	assert(widget_set(column, PROP_BOX_GAP, &gap));
	widget_add(column, top);
	widget_add(column, space);
	widget_add(column, bottom);
	widget_measure_tree(column);
	assert(column->min_w == 4 && column->min_h == 9);
	widget_layout_tree(column, 0, 0, 10, 9);
	assert(space->ly == 3 && space->w == 10 && space->h == 3);
	assert(bottom->ly == 8);
	space->flex_h = 1;
	widget_measure_tree(column);
	widget_layout_tree(column, 0, 0, 10, 13);
	assert(space->h == 7 && bottom->ly == 12);
	widget_free(column);
}

static void test_empty_and_single(void)
{
	assert(!make_spacer(-1, 0));
	assert(!make_spacer(0, -1));

	for (int vertical = 0; vertical < 2; vertical++) {
		struct widget *box;

		if (vertical)
			box = make_vbox();
		else
			box = make_hbox();

		assert(box);
		int gap = 4096;
		assert(widget_set(box, PROP_BOX_GAP, &gap));
		widget_measure_tree(box);
		assert(box->min_w == 0 && box->min_h == 0);
		widget_layout_tree(box, 0, 0, 1, 1);
		struct widget *space = make_spacer(2, 3);
		assert(space);
		widget_add(box, space);
		widget_measure_tree(box);
		assert(box->min_w == 2 && box->min_h == 3);
		widget_layout_tree(box, 0, 0, 2, 3);
		assert(space->lx == 0 && space->ly == 0 && space->w == 2 && space->h == 3);
		widget_free(box);
	}
}

static void test_scroll_content(void)
{
	struct widget *pad = make_pad_box();
	struct widget *column = make_vbox();
	struct widget *top = make_label(L"Top");
	struct widget *space = make_spacer(0, 8);
	struct widget *bottom = make_input(L"last", NULL);
	assert(pad && column && top && space && bottom);
	int gap = 2;
	assert(widget_set(column, PROP_BOX_GAP, &gap));
	widget_add(column, top);
	widget_add(column, space);
	widget_add(column, bottom);
	widget_add(pad, column);
	widget_measure_tree(pad);
	widget_layout_tree(pad, 0, 0, 12, 4);
	int content, offset;
	assert(widget_get(pad, PROP_SCROLL_CONTENT_H, &content) && content == 14);
	pad->ops->ensure_visible(pad, bottom);
	assert(widget_get(pad, PROP_SCROLL_Y, &offset) && offset == 10);
	pad->ops->ensure_visible(pad, top);
	assert(widget_get(pad, PROP_SCROLL_Y, &offset) && offset == 0);
	widget_free(pad);
}

static void test_node_ids(void)
{
	struct widget *root = make_vbox();
	struct widget *row = make_hbox();
	struct widget *input = make_input(L"", NULL);
	struct widget *other = make_input(L"", NULL);
	assert(root && row && input && other);
	widget_add(root, row);
	widget_add(row, input);
	char name[] = "host";
	assert(widget_set_node_id(input, name));
	name[0] = 'X';
	assert(strcmp(input->node_id, "host") == 0);
	assert(widget_set_node_id(other, "host"));
	assert(find_widget_by_node_id(root, "host") == input);
	assert(find_widget_by_node_id(other, "host") == other);
	assert(!find_widget_by_node_id(row, "missing"));
	assert(!find_widget_by_node_id(NULL, "host"));
	assert(!find_widget_by_node_id(root, NULL));
	assert(widget_set_node_id(input, input->node_id));
	assert(!widget_set_node_id(input, "bad name"));
	assert(!widget_set_node_id(input, ""));
	assert(!widget_set_node_id(input, NULL));
	assert(strcmp(input->node_id, "host") == 0);
	char limit[WIDGET_NODE_ID_MAX + 2];
	memset(limit, 'a', sizeof(limit));
	limit[sizeof(limit) - 1] = '\0';
	assert(!widget_set_node_id(input, limit));
	assert(find_widget_by_node_id(root, "host") == input);
	limit[WIDGET_NODE_ID_MAX] = '\0';
	assert(widget_set_node_id(input, limit));
	assert(!find_widget_by_node_id(root, "host"));
	assert(find_widget_by_node_id(root, limit) == input);
	assert(widget_set_node_id(input, "host-1.example_2"));
	assert(find_widget_by_node_id(root, "host-1.example_2") == input);
	widget_free(root);
	widget_free(other);
}

int main(void)
{
	test_horizontal();
	test_vertical();
	test_empty_and_single();
	test_scroll_content();
	test_node_ids();
	return 0;
}
