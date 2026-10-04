// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>

#include <curses.h>

#include "widget.h"

static void check_style(struct widget *w, attr_t expected, short expected_pair)
{
	cchar_t cell;
	wchar_t text[CCHARW_MAX];
	attr_t attrs;
	short pair;

	assert(mvwin_wch(w->win, 0, 0, &cell) == OK);
	assert(getcchar(&cell, text, &attrs, &pair, NULL) == OK);
	assert((attrs & (A_BOLD | A_DIM | A_UNDERLINE | A_REVERSE)) == expected);
	assert(pair == expected_pair);
}

int main(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	assert(start_color() == OK);
	assert(init_pair(COLOR_PAIR_WINDOW, COLOR_WHITE, COLOR_BLUE) == OK);
	assert(init_pair(COLOR_PAIR_BUTTON, COLOR_BLACK, COLOR_WHITE) == OK);
	assert(init_pair(COLOR_PAIR_FOCUS, COLOR_WHITE, COLOR_GREEN) == OK);

	struct widget *button = make_button(L"OK");
	assert(button);
	widget_measure_tree(button);
	widget_layout_tree(button, 0, 0, 10, 1);
	widget_render_tree(button);
	check_style(button, A_NORMAL, COLOR_PAIR_BUTTON);
	WINDOW *original = button->win;

	widget_style_set_attrs(COLOR_PAIR_BUTTON, A_BOLD);
	widget_style_set_attrs(COLOR_PAIR_FOCUS, A_UNDERLINE | A_DIM);
	widget_render_tree(button);
	assert(button->win == original);
	check_style(button, A_BOLD, COLOR_PAIR_BUTTON);
	button->flags |= FLAG_INFOCUS;
	widget_render_tree(button);
	check_style(button, A_UNDERLINE | A_DIM, COLOR_PAIR_FOCUS);
	button->flags &= ~FLAG_INFOCUS;
	widget_render_tree(button);
	check_style(button, A_BOLD, COLOR_PAIR_BUTTON);
	widget_style_set_attrs(COLOR_PAIR_BUTTON, A_NORMAL);
	widget_render_tree(button);
	check_style(button, A_NORMAL, COLOR_PAIR_BUTTON);

	widget_style_set_attrs(COLOR_PAIR_WINDOW, A_BOLD);
	struct widget *border = make_border();
	assert(border);
	widget_measure_tree(border);
	widget_layout_tree(border, 0, 0, 10, 4);
	widget_render_tree(border);
	check_style(border, A_BOLD, COLOR_PAIR_WINDOW);
	widget_style_set_attrs(COLOR_PAIR_WINDOW, A_NORMAL);
	widget_render_tree(border);
	check_style(border, A_NORMAL, COLOR_PAIR_WINDOW);

	widget_style_apply(button->win, COLOR_PAIR_BUTTON);
	widget_scrollbar_draw(button->win, COLOR_PAIR_FOCUS, 0, 20, false);
	attr_t attrs;
	short pair;
	assert(wattr_get(button->win, &attrs, &pair, NULL) == OK);
	assert((attrs & ~A_COLOR) == A_NORMAL && pair == COLOR_PAIR_BUTTON);

	widget_free(border);
	widget_free(button);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
