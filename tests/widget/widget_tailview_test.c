// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <wchar.h>

#include "widget.h"

static void check_cell(struct widget *w, int y, int x, wchar_t expected)
{
	cchar_t cell;
	wchar_t text[CCHARW_MAX];
	attr_t attrs;
	short pair;
	assert(mvwin_wch(w->win, y, x, &cell) == OK);
	assert(getcchar(&cell, text, &attrs, &pair, NULL) == OK);
	assert(text[0] == expected);
}

int main(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	struct widget *view = make_tailview();
	assert(view);
	assert(widget_set(view, PROP_TEXT_VALUE, L"first\nsecond\nthird\n"));
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 10, 2);
	widget_render_tree(view);
	check_cell(view, 0, 0, L's');
	check_cell(view, 1, 0, L't');
	assert(view->ops->input(view, KEY_RIGHT));
	widget_render_tree(view);
	check_cell(view, 0, 0, L'e');
	assert(view->ops->input(view, L'0'));
	assert(view->ops->input(view, KEY_LEFT));
	int x = -1;
	assert(widget_get(view, PROP_SCROLL_X, &x) && x == 0);
	x = -1;
	assert(!widget_set(view, PROP_SCROLL_X, &x));
	/* Text replacement owns its copy and clears obsolete screen cells. */
	wchar_t value[] = L"new";
	assert(widget_set(view, PROP_TEXT_VALUE, value));
	value[0] = L'X';
	widget_render_tree(view);
	check_cell(view, 0, 0, L'n');
	check_cell(view, 0, 3, L' ');
	check_cell(view, 1, 0, L' ');
	assert(widget_set(view, PROP_TEXT_VALUE, L""));
	widget_render_tree(view);
	check_cell(view, 0, 0, L' ');
	widget_free(view);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
