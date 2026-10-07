// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>

#include "widget.h"

static int property(struct widget *w, enum widget_property prop)
{
	int value;
	assert(widget_get(w, prop, &value));
	return value;
}

int main(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	struct widget *view = make_textview(L"012345678901234567890123456789\nTwo\nThree\nFour\nFive\nSix");
	assert(view && view->type == WIDGET_TEXTVIEW);
	struct widget *pad = find_widget_by_type_and_id(view, WIDGET_PAD_BOX, 0);
	struct widget *scroll = find_widget_by_type_and_id(view, WIDGET_SCROLL_VBOX, 0);
	assert(pad && scroll);
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 12, 4);
	assert(property(pad, PROP_SCROLL_CONTENT_H) == 6);
	assert(property(pad, PROP_SCROLL_CONTENT_W) == 30);
	assert(widget_dispatch_input(scroll, KEY_END, true));
	assert(property(pad, PROP_SCROLL_Y) > 0);
	assert(widget_dispatch_input(scroll, KEY_RIGHT, true));
	assert(property(pad, PROP_SCROLL_X) == 1);
	struct widget_keybinding keys[16];
	assert(widget_keybindings(scroll, keys, 16) == 8);
	assert(!widget_set(view, PROP_INPUT_VALUE, L"bad"));
	assert(widget_set(view, PROP_TEXT_VALUE, L"Short"));
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 12, 4);
	assert(property(pad, PROP_SCROLL_Y) == 0);
	assert(property(pad, PROP_SCROLL_X) == 0);
	assert(property(pad, PROP_SCROLL_CONTENT_H) == 1);
	assert(property(pad, PROP_SCROLL_CONTENT_W) == 5);
	assert(widget_set(view, PROP_TEXT_VALUE, L""));
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 12, 4);
	assert(property(pad, PROP_SCROLL_CONTENT_H) == 0);
	assert(widget_set(view, PROP_TEXT_VALUE, L"One\nTwo\nThree\nFour\nFive\nSix\nSeven"));
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 12, 4);
	assert(property(pad, PROP_SCROLL_CONTENT_H) == 7);
	assert(widget_dispatch_input(scroll, KEY_NPAGE, true));
	assert(property(pad, PROP_SCROLL_Y) > 0);
	widget_free(view);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
