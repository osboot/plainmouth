// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <curses.h>
#include <panel.h>

#include "daemon_help.h"
#include "widget.h"

static const char *description(struct widget *w, wchar_t key)
{
	struct widget_keybinding bindings[32];
	size_t n = widget_keybindings(w, bindings, sizeof(bindings) / sizeof(*bindings));
	const char *result = NULL;
	for (size_t i = 0; i < n; i++) {
		if (bindings[i].key == key && bindings[i].keycode) {
			assert(!result);
			result = bindings[i].description;
		}
	}
	return result;
}

static void test_context(void)
{
	struct widget *scroll = make_scroll_vbox();
	struct widget *input = make_input(L"unchanged", NULL);
	assert(scroll && input);
	widget_add(scroll, input);
	assert(strcmp(description(input, KEY_HOME), "Beginning of line") == 0);
	assert(strcmp(description(input, KEY_END), "End of line") == 0);
	assert(strcmp(description(input, KEY_PPAGE), "Scroll one page up") == 0);
	assert(strcmp(description(input, KEY_DOWN), "Scroll down") == 0);
	assert(strcmp(description(input, KEY_LEFT), "Move cursor left") == 0);
	bool finish = false;
	assert(widget_set(input, PROP_INPUT_FINISH_ON_ENTER, &finish));
	assert(!description(input, KEY_ENTER));
	input->attrs |= ATTR_READONLY;
	assert(!description(input, KEY_HOME));
	input->attrs &= ~ATTR_READONLY;
	widget_free(scroll);
	struct widget *menu = make_menu(3);
	struct widget *select = make_select(2, 3);
	assert(menu && select);
	assert(!description(menu, KEY_ENTER));
	widget_add(menu, make_menu_option(L"first"));
	widget_add(select, make_select_option(L"first", false, true));
	assert(strcmp(description(menu, KEY_ENTER), "Accept selected item") == 0);
	assert(!description(select, KEY_ENTER));
	assert(strcmp(description(menu, KEY_PPAGE), "Previous page") == 0);
	widget_free(menu);
	widget_free(select);
}

static void assert_text(WINDOW *win, int row, const char *expected)
{
	char text[64];
	assert(mvwinnstr(win, row, 1, text, getmaxx(win) - 2) != ERR);
	assert(strncmp(text, expected, strlen(expected)) == 0);
}

static void test_panel(void)
{
	struct widget *root = make_window();
	struct widget *input = make_input(L"unchanged", NULL);
	assert(root && input);
	widget_add(root, input);
	widget_measure_tree(root);
	widget_layout_tree(root, 0, 0, 30, 1);
	widget_render_tree(root);
	struct daemon_help help = { 0 };
	daemon_help_render(&help, input);
	assert(!help.open && help.panel);
	assert(getbegx(help.win) == 73 && getbegy(help.win) == 7);
	assert(getmaxx(help.win) == 7 && getmaxy(help.win) == 1);
	for (int i = 0; i < 7; i++)
		assert((mvwinch(help.win, 0, i) & A_CHARTEXT) == (chtype) "F1 Help"[i]);
	assert(!daemon_help_input(&help, input, KEY_DOWN, true));
	assert(daemon_help_input(&help, input, KEY_F(1), true));
	daemon_help_render(&help, input);
	assert(help.open && help.panel && help.offset == 0);
	assert(getbegx(help.win) == 36);
	assert_text(help.win, 1, "Tab      : Next field");
	assert((mvwinch(help.win, 1, 1) & A_BOLD) != 0);
	assert(!(mvwinch(help.win, 1, 10) & A_BOLD));
	assert((mvwinch(help.win, 1, 10) & A_CHARTEXT) == ':');
	assert((mvwinch(help.win, 2, 10) & A_CHARTEXT) == ':');
	assert(panel_below(NULL) == help.panel);
	assert(daemon_help_input(&help, input, KEY_DOWN, true));
	daemon_help_render(&help, input);
	assert(help.offset == 1);
	assert_text(help.win, 1, "Shift-Tab: Previous field");
	assert((mvwinch(help.win, 1, 9) & A_BOLD) != 0);
	for (int i = 0; i < 50; i++)
		assert(daemon_help_input(&help, input, KEY_DOWN, true));
	daemon_help_render(&help, input);
	assert(help.offset == help.lines - (getmaxy(help.win) - 2));
	for (int i = 0; i < 50; i++)
		assert(daemon_help_input(&help, input, KEY_UP, true));
	assert(help.offset == 0);
	const wchar_t *value;
	assert(widget_get(input, PROP_INPUT_VALUE, &value));
	assert(wcscmp(value, L"unchanged") == 0);
	assert(!daemon_help_input(&help, input, L'\t', false));
	struct widget *scroll = make_scroll_vbox();
	assert(scroll);
	daemon_help_render(&help, scroll);
	assert(help.open && help.context == scroll && help.offset == 0);
	assert(resize_term(6, 20) == OK);
	daemon_help_render(&help, input);
	assert(getmaxx(help.win) == 20 && getbegx(help.win) == 0);
	assert(getmaxy(help.win) == 6 && help.lines > 4);
	assert_text(help.win, 1, "Tab      : Next fi");
	assert_text(help.win, 2, "           eld");
	assert(!(mvwinch(help.win, 2, 12) & A_BOLD));
	assert(resize_term(1, 1) == OK);
	daemon_help_render(&help, input);
	assert(help.open && !help.panel);
	assert(resize_term(8, 80) == OK);
	daemon_help_render(&help, input);
	assert(help.panel);
	assert(daemon_help_input(&help, input, 27, false));
	assert(!help.open && !help.panel && !help.win);
	daemon_help_render(&help, input);
	assert(!help.open && help.panel && getbegy(help.win) == 7);
	assert(resize_term(6, 20) == OK);
	daemon_help_render(&help, input);
	assert(getbegx(help.win) == 13 && getbegy(help.win) == 5);
	assert(resize_term(1, 1) == OK);
	daemon_help_render(&help, input);
	assert(!help.panel);
	assert(resize_term(8, 80) == OK);
	assert(daemon_help_input(&help, input, KEY_F(1), true));
	daemon_help_render(&help, input);
	assert(daemon_help_input(&help, input, KEY_F(1), true));
	assert(!help.open);
	struct widget *terminal = widget_create(WIDGET_TERMINAL);
	assert(terminal);
	assert(!daemon_help_input(&help, terminal, KEY_F(1), true));
	assert(!help.open);
	assert(daemon_help_input(&help, input, KEY_F(1), true));
	daemon_help_render(&help, terminal);
	assert(!help.open && !help.panel);
	widget_free(terminal);
	widget_free(scroll);
	widget_free(root);
}

int main(void)
{
	test_context();
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	assert(resize_term(8, 80) == OK);
	test_panel();
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
