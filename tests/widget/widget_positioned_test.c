// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include <curses.h>

#include "widget.h"

static void test_layout(void)
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
}

static void check_cell(WINDOW *win, int y, int x, wchar_t expected, short pair)
{
	cchar_t cell;
	wchar_t text[CCHARW_MAX];
	attr_t attrs;
	short actual_pair;
	assert(mvwin_wch(win, y, x, &cell) == OK);
	assert(getcchar(&cell, text, &attrs, &actual_pair, NULL) == OK);
	assert(text[0] == expected && text[1] == L'\0');
	assert(actual_pair == pair);
}

static void test_field_context(void)
{
	struct widget *pad = make_pad_box();
	struct widget *canvas = make_positioned();
	struct widget *label = make_label(L"Name:");
	struct widget *field = make_input(L"top", NULL);
	struct widget *bottom = make_input(L"bottom", NULL);
	assert(pad && canvas && label && field && bottom);
	widget_add(pad, canvas);
	assert(positioned_add(canvas, label, 0, 0, 6, 1));
	assert(positioned_add(canvas, field, 7, 0, 10, 1));
	assert(positioned_add(canvas, bottom, 35, 15, 10, 1));
	widget_measure_tree(pad);
	widget_layout_tree(pad, 0, 0, 27, 5);
	pad->ops->ensure_visible(pad, bottom);
	int x;
	assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 18);
	pad->ops->ensure_visible(pad, field);
	assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 0);
	pad->ops->ensure_visible(pad, field);
	assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 0);

	/* A narrow viewport must prioritize the editable field. */
	widget_layout_tree(pad, 0, 0, 10, 5);
	pad->ops->ensure_visible(pad, field);
	assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 7);
	widget_free(pad);
}

static void test_nested_visibility(void)
{
	struct widget *outer = make_pad_box();
	struct widget *canvas = make_positioned();
	struct widget *inner = make_scroll_vbox();
	struct widget *fields = make_positioned();
	struct widget *field = make_input(L"value", NULL);
	assert(outer && canvas && inner && fields && field);
	widget_add(outer, canvas);
	assert(positioned_add(canvas, inner, 0, 8, 10, 5));
	widget_add(inner, fields);
	assert(positioned_add(fields, field, 0, 15, 5, 1));
	widget_measure_tree(outer);
	widget_layout_tree(outer, 0, 0, 10, 10);
	struct widget *pad = find_widget_by_type_and_id(inner, WIDGET_PAD_BOX, 0);
	assert(pad);
	pad->ops->ensure_visible(pad, field);
	outer->ops->ensure_visible(outer, field);
	int y;
	assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 11);
	assert(widget_get(outer, PROP_SCROLL_Y, &y) && y == 3);
	widget_free(outer);
}

static void test_render(bool oversized)
{
	struct widget *root = make_window();
	struct widget *scroll = make_scroll_vbox();
	struct widget *canvas = make_positioned();
	struct widget *empty = make_input(L"", NULL);
	assert(root && scroll && canvas && empty);
	widget_add(root, scroll);
	widget_add(scroll, canvas);
	assert(positioned_add(canvas, empty, 2, 1, 6, 1));
	if (oversized) {
		struct widget *bottom = make_input(L"bottom", NULL);
		assert(bottom && positioned_add(canvas, bottom, 35, 15, 10, 1));
	}
	int yellow = COLOR_YELLOW, cyan = COLOR_CYAN;
	assert(widget_style_override(root, COLOR_PAIR_WINDOW, &yellow, &cyan, NULL, false));
	widget_measure_tree(root);
	widget_layout_tree(root, 5, 5, 20, 8);
	werase(stdscr);
	wnoutrefresh(stdscr);
	widget_render_tree(root);
	int cursor_y, cursor_x;
	assert(widget_coordinates_yx(empty, &cursor_y, &cursor_x));
	assert(cursor_y == 6 && cursor_x == 7);
	assert(widget_set(empty, PROP_INPUT_VALUE, L"abcdef"));
	widget_render_tree(empty);
	assert(widget_coordinates_yx(empty, &cursor_y, &cursor_x));
	assert(cursor_y == 6 && cursor_x == 12);
	assert(widget_set(empty, PROP_INPUT_VALUE, L""));
	widget_render_tree(empty);
	check_cell(newscr, 0, 0, L' ', 0);
	struct widget *pad = find_widget_by_type_and_id(root, WIDGET_PAD_BOX, 0);
	struct widget *vscroll = find_widget_by_type_and_id(root, WIDGET_VSCROLL, 0);
	struct widget *hscroll = find_widget_by_type_and_id(root, WIDGET_HSCROLL, 0);
	assert(pad && vscroll && hscroll);
	attr_t attrs;
	short pair;
	assert(widget_style_resolve(canvas, COLOR_PAIR_WINDOW, &attrs, &pair));
	check_cell(canvas->win, 0, 0, L' ', pair);
	check_cell(pad->win, 0, 0, L' ', pair);
	check_cell(pad->win, pad->h - 1, pad->w - 1, L' ', pair);
	for (int x = 2; x < 8; x++)
		check_cell(pad->win, 1, x, L' ', COLOR_PAIR_INPUT);
	assert(empty->ops->input_event(empty, L'a', false));
	widget_render_tree(empty);
	check_cell(pad->win, 1, 2, L'a', COLOR_PAIR_INPUT);
	check_cell(newscr, 0, 0, L' ', 0);
	assert(widget_set(empty, PROP_INPUT_VALUE, L""));
	widget_render_tree(empty);
	for (int x = 2; x < 8; x++)
		check_cell(pad->win, 1, x, L' ', COLOR_PAIR_INPUT);
	if (oversized) {
		assert(scroll->attrs & ATTR_CAN_FOCUS);
		scroll->flags |= FLAG_INFOCUS;
		widget_render_tree(scroll);
		check_cell(vscroll->win, 0, 0, L'^', COLOR_PAIR_FOCUS);
		check_cell(hscroll->win, 0, 0, L'<', COLOR_PAIR_FOCUS);
		assert(scroll->ops->input(scroll, KEY_DOWN));
		assert(scroll->ops->input(scroll, KEY_RIGHT));
		widget_render_tree(scroll);
		int x, y;
		assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 1);
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 1);
		assert(widget_coordinates_yx(empty, &cursor_y, &cursor_x));
		assert(cursor_y == 5 && cursor_x == 6);
		int hidden_y = 2;
		assert(widget_set(pad, PROP_SCROLL_Y, &hidden_y));
		assert(!widget_coordinates_yx(empty, &cursor_y, &cursor_x));
		hidden_y = 1;
		assert(widget_set(pad, PROP_SCROLL_Y, &hidden_y));
		check_cell(pad->win, 0, 1, L' ', COLOR_PAIR_INPUT);
		scroll->flags &= ~FLAG_INFOCUS;
		widget_render_tree(scroll);
		assert(!(vscroll->flags & FLAG_INFOCUS));
		assert(!(hscroll->flags & FLAG_INFOCUS));
		int start = 0;
		assert(widget_set(pad, PROP_SCROLL_Y, &start));
		assert(widget_dispatch_input(empty, KEY_NPAGE, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == pad->h);
		assert(widget_dispatch_input(empty, KEY_PPAGE, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 0);
		assert(widget_dispatch_input(empty, KEY_DOWN, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 1);
		assert(widget_dispatch_input(empty, KEY_HOME, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 1);
		assert(widget_dispatch_input(scroll, KEY_END, true));
		int content_h;
		assert(widget_get(pad, PROP_SCROLL_CONTENT_H, &content_h));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == content_h - pad->h);
		assert(widget_dispatch_input(scroll, KEY_NPAGE, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == content_h - pad->h);
		assert(widget_dispatch_input(scroll, KEY_HOME, true));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 0);
		assert(!widget_dispatch_input(scroll, KEY_DOWN, false));
		assert(widget_get(pad, PROP_SCROLL_Y, &y) && y == 0);
		const wchar_t *value;
		assert(widget_get(empty, PROP_INPUT_VALUE, &value) && value[0] == L'\0');
	}
	widget_free(root);
}

int main(void)
{
	test_layout();
	test_field_context();
	test_nested_visibility();
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen && start_color() == OK);
	assert(init_pair(COLOR_PAIR_WINDOW, COLOR_WHITE, COLOR_BLUE) == OK);
	assert(init_pair(COLOR_PAIR_INPUT, COLOR_BLACK, COLOR_WHITE) == OK);
	assert(init_pair(COLOR_PAIR_FOCUS, COLOR_WHITE, COLOR_GREEN) == OK);
	test_render(false);
	test_render(true);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
