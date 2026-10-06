// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <locale.h>
#include <time.h>

#include <curses.h>

#include "widget.h"

static void assert_cursor(struct widget *w, int expected)
{
	int index = -1;
	assert(widget_get(w, PROP_SELECT_CURSOR, &index));
	assert(index == expected);
}

static void test_navigation(int maximum)
{
	struct widget *w = maximum ? make_select(maximum, 2) : make_menu(2);
	struct widget *root = make_window();
	const wchar_t *labels[] = { L"Alpha", L"Alpine", L"Beta", L"Bravo", L"Gamma", L"\u03a9mega" };
	assert(w && root);
	widget_add(root, w);
	w->flags |= FLAG_INFOCUS;
	for (int n = 0; n < 6; n++) {
		struct widget *option;
		if (maximum)
			option = make_select_option(labels[n], n == 0, maximum > 1);
		else
			option = make_menu_option(labels[n]);
		assert(option);
		widget_add(w, option);
	}
	widget_measure_tree(root);
	widget_layout_tree(root, 0, 0, 20, 2);
	widget_render_tree(root);
	assert(w->ops->input_event(w, KEY_NPAGE, true));
	assert_cursor(w, 2);
	assert(w->ops->input_event(w, KEY_END, true));
	assert_cursor(w, 5);
	assert(w->ops->input_event(w, KEY_PPAGE, true));
	assert_cursor(w, 3);
	assert(w->ops->input_event(w, KEY_HOME, true));
	assert_cursor(w, 0);
	assert(w->ops->input_event(w, L'a', false));
	assert_cursor(w, 1);
	assert(w->ops->input_event(w, L'l', false));
	assert_cursor(w, 1);
	assert(w->ops->input_event(w, L'p', false));
	assert(w->ops->input_event(w, L'h', false));
	assert_cursor(w, 0);
	widget_render_tree(w);
	struct widget *active = find_widget_by_type_and_id(w, WIDGET_LABEL, 0);
	assert(active && active->win);
	for (int n = 0; n < 4; n++)
		assert((mvwinch(active->win, 0, n) & A_UNDERLINE) != 0);
	assert(!(mvwinch(active->win, 0, 4) & A_UNDERLINE));
	assert(w->ops->input_event(w, KEY_HOME, true));
	widget_render_tree(w);
	assert(!(mvwinch(active->win, 0, 0) & A_UNDERLINE));
	assert(w->ops->input_event(w, L'B', false));
	assert_cursor(w, 2);
	assert(w->ops->input_event(w, L'b', false));
	assert_cursor(w, 3);
	assert(w->ops->input_event(w, L'b', false));
	assert_cursor(w, 2);
	assert(!w->ops->input_event(w, L'z', false));
	assert_cursor(w, 2);
	assert(w->ops->input_event(w, L'\u03c9', false));
	assert_cursor(w, 5);
	struct widget *list = find_widget_by_type_and_id(w, WIDGET_LIST_VBOX, 0);
	int scroll_y;
	assert(list && widget_get(list, PROP_SCROLL_Y, &scroll_y));
	assert(scroll_y > 0);
	struct widget *last = TAILQ_LAST(&list->children, widgethead);
	assert(last && (last->flags & FLAG_VISIBLE));
	/* Relayout must retain the cursor and include it in the new viewport. */
	widget_hide_tree(root);
	widget_layout_tree(root, 0, 0, 20, 1);
	widget_render_tree(root);
	assert_cursor(w, 5);
	assert(last->flags & FLAG_VISIBLE);
	assert(last->win && getmaxy(last->win) == 1);
	assert(widget_get(list, PROP_SCROLL_Y, &scroll_y) && scroll_y == 5);
	widget_hide_tree(root);
	widget_layout_tree(root, 0, 0, 20, 4);
	widget_render_tree(root);
	assert_cursor(w, 5);
	assert(last->flags & FLAG_VISIBLE);
	assert(widget_get(list, PROP_SCROLL_Y, &scroll_y) && scroll_y == 2);
	assert(!w->ops->input_event(w, KEY_F(1), true));
	if (!maximum) {
		assert(widget_select_search_timeout(w) > 0);
		struct timespec pause = { .tv_sec = 1, .tv_nsec = 100000000 };
		assert(nanosleep(&pause, NULL) == 0);
		assert(widget_select_search_timeout(w) == 0);
		widget_render_tree(w);
		assert(widget_select_search_timeout(w) == -1);
		assert(w->ops->input_event(w, L'G', false));
		w->flags &= ~FLAG_INFOCUS;
		widget_render_tree(w);
		assert(widget_select_search_timeout(w) == -1);
	}
	if (maximum) {
		for (int n = 0; n < 6; n++) {
			bool checked;
			assert(widget_get_index(w, PROP_SELECT_OPTION_VALUE, n, &checked));
			assert(checked == (n == 0));
		}
		assert(w->ops->input_event(w, KEY_HOME, true));
		assert(w->ops->input_event(w, L' ', false));
		bool checked = true;
		assert(widget_get_index(w, PROP_SELECT_OPTION_VALUE, 0, &checked));
		assert(!checked);
	}
	widget_free(root);
}

static void test_wide_prefix(void)
{
	struct widget *root = make_window(), *menu = make_menu(1);
	struct widget *option = make_menu_option(L"\u754c\u0301tail");
	assert(root && menu && option);
	widget_add(root, menu);
	widget_add(menu, option);
	menu->flags |= FLAG_INFOCUS;
	widget_measure_tree(root);
	widget_layout_tree(root, 0, 0, 20, 1);
	widget_render_tree(root);
	assert(menu->ops->input_event(menu, L'\u754c', false));
	widget_render_tree(menu);
	struct widget *label = find_widget_by_type_and_id(option, WIDGET_LABEL, 0);
	assert(label && label->win);
	for (int x = 0; x < 3; x++) {
		cchar_t cell;
		wchar_t text[CCHARW_MAX];
		attr_t attrs;
		short pair;
		assert(mvwin_wch(label->win, 0, x, &cell) == OK);
		assert(getcchar(&cell, text, &attrs, &pair, NULL) == OK);
		assert(((attrs & A_UNDERLINE) != 0) == (x < 2));
		if (x == 0)
			assert(text[0] == L'\u754c' && text[1] == L'\u0301');
	}
	widget_free(root);
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	test_navigation(0);
	test_navigation(1);
	test_navigation(3);
	test_wide_prefix();
	struct widget *menu = make_menu(2);
	assert(menu);
	for (int i = 0; i < 5; i++) {
		struct widget *option = make_menu_option(L"entry");
		assert(option);
		widget_add(menu, option);
	}
	widget_measure_tree(menu);
	widget_layout_tree(menu, 0, 0, 20, 2);
	assert_cursor(menu, 0);
	assert(menu->ops->input(menu, KEY_DOWN));
	assert_cursor(menu, 1);
	assert(menu->ops->input(menu, KEY_NPAGE));
	assert_cursor(menu, 3);
	assert(menu->ops->input(menu, KEY_END));
	assert_cursor(menu, 4);
	assert(menu->ops->input(menu, KEY_DOWN));
	assert_cursor(menu, 4);
	assert(menu->ops->input(menu, KEY_HOME));
	assert_cursor(menu, 0);
	assert(menu->ops->input(menu, KEY_UP));
	assert_cursor(menu, 0);
	int index = 3;
	assert(widget_set(menu, PROP_SELECT_CURSOR, &index));
	index = 99;
	assert(!widget_set(menu, PROP_SELECT_CURSOR, &index));
	assert_cursor(menu, 3);
	assert(menu->ops->input(menu, L' '));
	assert_cursor(menu, 3);
	bool finished = true;
	assert(widget_get(menu, PROP_SELECT_STATE, &finished));
	assert(!finished);
	assert(menu->ops->input(menu, L'\n'));
	assert(widget_get(menu, PROP_SELECT_STATE, &finished));
	assert(finished);
	widget_free(menu);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
