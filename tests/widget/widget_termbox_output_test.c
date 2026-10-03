// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "widget.h"
#include "plugins/termbox-output.h"

struct replies {
	char text[128];
	size_t length;
	bool fail;
};

static bool reply(void *data, const char *text, size_t length)
{
	struct replies *responses = data;
	if (responses->fail)
		return false;
	assert(length < sizeof(responses->text) - responses->length);
	memcpy(responses->text + responses->length, text, length);
	responses->length += length;
	responses->text[responses->length] = '\0';
	return true;
}

static void feed(struct widget *view, const char *text)
{
	assert(termbox_output_feed(view, text, strlen(text)));
	widget_render_tree(view);
}

static void check_cell(struct widget *view, int y, int x, wchar_t expected, attr_t required)
{
	cchar_t cell;
	wchar_t text[CCHARW_MAX];
	attr_t attrs;
	short pair;
	assert(mvwin_wch(view->win, y, x, &cell) == OK);
	assert(getcchar(&cell, text, &attrs, &pair, NULL) == OK);
	assert(text[0] == expected);
	assert((attrs & required) == required);
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	assert(start_color() == OK);
	assert(init_pair(COLOR_PAIR_WINDOW, COLOR_WHITE, COLOR_BLUE) == OK);
	struct widget *view = make_termbox_output();
	assert(view);
	widget_measure_tree(view);
	widget_layout_tree(view, 0, 0, 12, 4);
	struct replies responses = { 0 };
	assert(termbox_output_start(view, reply, &responses));
	feed(view, "long text\rhi\033[K");
	check_cell(view, 0, 0, L'h', 0);
	check_cell(view, 0, 2, L' ', 0);
	assert(view->attrs & ATTR_CAN_CURSOR);
	assert(getcury(view->win) == 0 && getcurx(view->win) == 2);
	feed(view, "\033[2;3H\033[1;4;31mR\033[0m");
	check_cell(view, 1, 2, L'R', A_BOLD | A_UNDERLINE);
	cchar_t red;
	wchar_t chars[CCHARW_MAX];
	attr_t attrs;
	short pair, fg, bg;
	assert(mvwin_wch(view->win, 1, 2, &red) == OK);
	assert(getcchar(&red, chars, &attrs, &pair, NULL) == OK);
	assert(pair_content(pair, &fg, &bg) == OK && fg == COLOR_RED);
	feed(view, "\033[?25l");
	assert(!(view->attrs & ATTR_CAN_CURSOR));
	feed(view, "\033[?25h\033[6n");
	assert(!strcmp(responses.text, "\033[2;4R"));
	feed(view, "\033[?1049hALT");
	check_cell(view, 0, 0, L' ', 0);
	feed(view, "\033[?1049l");
	check_cell(view, 0, 0, L'h', 0);
	feed(view, "\033[2J\033[H\304");
	check_cell(view, 0, 0, L' ', 0);
	feed(view, "\203\344\270\255e\314\201");
	check_cell(view, 0, 0, L'\u0103', 0);
	check_cell(view, 0, 1, L'\u4e2d', 0);
	check_cell(view, 0, 3, L'e', 0);
	cchar_t combined;
	assert(mvwin_wch(view->win, 0, 3, &combined) == OK);
	assert(getcchar(&combined, chars, &attrs, &pair, NULL) == OK && chars[1] == L'\u0301');
	feed(view, "\033[2J\033[H123456789012X");
	check_cell(view, 1, 0, L'X', 0);
	feed(view, "\033[2J\033[Hone\r\ntwo\r\nthree\r\nfour\r\nfive");
	check_cell(view, 0, 0, L't', 0);
	check_cell(view, 3, 0, L'f', 0);
	struct widget *other = make_termbox_output();
	assert(other);
	widget_measure_tree(other);
	widget_layout_tree(other, 0, 0, 12, 4);
	assert(termbox_output_start(other, NULL, NULL));
	feed(other, "independent");
	check_cell(view, 3, 0, L'f', 0);
	widget_free(other);
	responses.fail = true;
	assert(!termbox_output_feed(view, "\033[6n", 4));
	termbox_output_finish(view);
	widget_render_tree(view);
	assert(!(view->attrs & ATTR_CAN_CURSOR));
	widget_free(view);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
