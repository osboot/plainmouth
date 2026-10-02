// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>

#include <curses.h>

#include "widget.h"

static void assert_cursor(struct widget *w, int expected)
{
	int index = -1;
	assert(widget_get(w, PROP_SELECT_CURSOR, &index));
	assert(index == expected);
}

int main(void)
{
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
	return 0;
}
