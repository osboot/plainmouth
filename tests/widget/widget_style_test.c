// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

static short check_colors(struct widget *w, attr_t expected, short expected_fg, short expected_bg)
{
	attr_t attrs;
	short pair, fg, bg;
	assert(widget_style_resolve(w, w->color_pair, &attrs, &pair));
	widget_render_tree(w);
	check_style(w, expected, pair);
	assert(pair_content(pair, &fg, &bg) == OK);
	assert(fg == expected_fg && bg == expected_bg);
	return pair;
}

static void test_theme(const char *term, enum widget_theme theme, short window_bg, short input_bg)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm(term, output, input);
	assert(screen && widget_style_init(theme));
	struct widget *root = make_window();
	struct widget *field = make_input(L"value", NULL);
	assert(root && field);
	widget_add(root, field);
	widget_measure_tree(root);
	widget_layout_tree(root, 2, 2, 20, 4);
	widget_render_tree(root);
	assert(field->color_pair == COLOR_PAIR_INPUT);
	attr_t attrs;
	short pair, fg, bg;
	assert(widget_style_resolve(root, COLOR_PAIR_WINDOW, &attrs, &pair));
	if (has_colors()) {
		assert(pair_content(pair, &fg, &bg) == OK && bg == window_bg);
		assert(widget_style_resolve(field, COLOR_PAIR_INPUT, &attrs, &pair));
		assert(pair_content(pair, &fg, &bg) == OK && bg == input_bg);
		assert(widget_style_resolve(root, COLOR_PAIR_MAIN, &attrs, &pair));
		assert(pair_content(pair, &fg, &bg) == OK);
		assert(bg != window_bg || theme == WIDGET_THEME_TERMINAL);
		attr_t underline = A_UNDERLINE;
		assert(widget_style_override(root, COLOR_PAIR_INPUT, NULL, NULL, &underline, false));
		assert(widget_style_resolve(field, COLOR_PAIR_INPUT, &attrs, &pair) && attrs == A_UNDERLINE);
		assert(widget_style_resolve(root, COLOR_PAIR_BUTTON, &attrs, &pair) && attrs != A_UNDERLINE);
	} else {
		assert(pair == 0 && (attrs & A_REVERSE));
		assert(widget_style_resolve(field, COLOR_PAIR_INPUT, &attrs, &pair));
		assert(pair == 0 && !(attrs & A_REVERSE));
	}
	widget_free(root);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
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
	assert(init_pair(COLOR_PAIR_READONLY, COLOR_WHITE, COLOR_BLUE) == OK);
	assert(init_pair(COLOR_PAIR_DISABLED, COLOR_WHITE, COLOR_BLACK) == OK);
	assert(init_pair(COLOR_PAIR_INVALID, COLOR_WHITE, COLOR_RED) == OK);
	assert(init_pair(COLOR_PAIR_INPUT, COLOR_WHITE, COLOR_BLACK) == OK);

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
	widget_scrollbar_draw(button, COLOR_PAIR_FOCUS, 0, 20, false);
	attr_t attrs;
	short pair;
	assert(wattr_get(button->win, &attrs, &pair, NULL) == OK);
	assert((attrs & ~A_COLOR) == A_NORMAL && pair == COLOR_PAIR_BUTTON);

	struct widget *root = make_window();
	struct widget *local = make_button(L"Local");
	assert(root && local);
	widget_add(root, local);
	widget_measure_tree(root);
	widget_layout_tree(root, 0, 0, 20, 5);
	int red = COLOR_RED;
	attr_t normal = A_NORMAL;
	assert(widget_style_override(root, COLOR_PAIR_BUTTON, &red, NULL, &normal, false));
	widget_render_tree(root);
	short local_pair = check_colors(local, A_NORMAL, COLOR_RED, COLOR_WHITE);
	assert(local_pair != COLOR_PAIR_BUTTON);
	check_colors(button, A_NORMAL, COLOR_BLACK, COLOR_WHITE);
	assert(init_pair(COLOR_PAIR_BUTTON, COLOR_GREEN, COLOR_BLUE) == OK);
	widget_style_set_attrs(COLOR_PAIR_BUTTON, A_DIM);
	check_colors(local, A_NORMAL, COLOR_RED, COLOR_BLUE);
	check_colors(button, A_DIM, COLOR_GREEN, COLOR_BLUE);
	assert(widget_style_override(root, COLOR_PAIR_BUTTON, NULL, NULL, NULL, true));
	assert(check_colors(local, A_DIM, COLOR_GREEN, COLOR_BLUE) == COLOR_PAIR_BUTTON);
	assert(widget_style_override(root, COLOR_PAIR_BUTTON, NULL, &red, NULL, false));
	assert(check_colors(local, A_DIM, COLOR_GREEN, COLOR_RED) == local_pair);
	widget_style_set_attrs(COLOR_PAIR_BUTTON, A_BOLD);
	check_colors(local, A_BOLD, COLOR_GREEN, COLOR_RED);

	struct widget *popup = make_button(L"Popup");
	assert(popup);
	popup->style_owner = local;
	widget_measure_tree(popup);
	widget_layout_tree(popup, 0, 0, 10, 1);
	assert(check_colors(popup, A_BOLD, COLOR_GREEN, COLOR_RED) == local_pair);
	widget_free(popup);

	/* Exhaustion must reject a new override without modifying its old state. */
	short *held = calloc((size_t) COLOR_PAIRS, sizeof(*held));
	assert(held);
	size_t count = 0;
	short allocated;
	while ((allocated = widget_color_pair_alloc()) >= 0)
		held[count++] = allocated;
	assert(!widget_style_override(button, COLOR_PAIR_BUTTON, &red, NULL, &normal, false));
	check_colors(button, A_BOLD, COLOR_GREEN, COLOR_BLUE);
	for (size_t i = 0; i < count; i++)
		widget_color_pair_free(held[i]);
	free(held);
	widget_free(root);
	assert(widget_style_override(button, COLOR_PAIR_BUTTON, &red, NULL, NULL, false));
	assert(check_colors(button, A_BOLD, COLOR_RED, COLOR_BLUE) == local_pair);

	struct widget *theme = widget_create(WIDGET_WINDOW);
	struct widget *left = make_button(L"Left"), *right = make_button(L"Right");
	assert(theme && left && right);
	int yellow = COLOR_YELLOW, white = COLOR_WHITE;
	attr_t underline = A_UNDERLINE, dim = A_DIM;
	assert(widget_style_override(theme, COLOR_PAIR_BUTTON, &yellow, NULL, &underline, false));
	left->style_owner = right->style_owner = theme;
	widget_measure_tree(left);
	widget_measure_tree(right);
	widget_layout_tree(left, 0, 0, 12, 1);
	widget_layout_tree(right, 0, 0, 12, 1);
	assert(!left->win && !right->win);
	short theme_pair = check_colors(left, A_UNDERLINE, COLOR_YELLOW, COLOR_BLUE);
	assert(check_colors(right, A_UNDERLINE, COLOR_YELLOW, COLOR_BLUE) == theme_pair);
	assert(widget_style_override(left, COLOR_PAIR_BUTTON, NULL, &red, &normal, false));
	check_colors(left, A_NORMAL, COLOR_YELLOW, COLOR_RED);
	assert(widget_style_override(theme, COLOR_PAIR_BUTTON, &white, NULL, &dim, false));
	check_colors(left, A_NORMAL, COLOR_WHITE, COLOR_RED);
	check_colors(right, A_DIM, COLOR_WHITE, COLOR_BLUE);
	assert(init_pair(COLOR_PAIR_BUTTON, COLOR_BLACK, COLOR_CYAN) == OK);
	check_colors(left, A_NORMAL, COLOR_WHITE, COLOR_RED);
	check_colors(right, A_DIM, COLOR_WHITE, COLOR_CYAN);
	assert(widget_style_override(theme, COLOR_PAIR_BUTTON, NULL, NULL, NULL, true));
	check_colors(left, A_NORMAL, COLOR_BLACK, COLOR_RED);
	assert(check_colors(right, A_BOLD, COLOR_BLACK, COLOR_CYAN) == COLOR_PAIR_BUTTON);
	assert(widget_style_override(left, COLOR_PAIR_BUTTON, NULL, NULL, NULL, true));
	assert(check_colors(left, A_BOLD, COLOR_BLACK, COLOR_CYAN) == COLOR_PAIR_BUTTON);
	assert(widget_style_override(theme, COLOR_PAIR_BUTTON, &red, NULL, &underline, false));
	check_colors(left, A_UNDERLINE, COLOR_RED, COLOR_CYAN);
	check_colors(right, A_UNDERLINE, COLOR_RED, COLOR_CYAN);
	widget_free(left);
	widget_free(right);
	widget_free(theme);

	struct widget *field = make_input(L"x", NULL);
	assert(field);
	widget_measure_tree(field);
	widget_layout_tree(field, 0, 0, 8, 1);
	widget_style_set_attrs(COLOR_PAIR_READONLY, A_UNDERLINE);
	widget_style_set_attrs(COLOR_PAIR_DISABLED, A_DIM);
	widget_style_set_attrs(COLOR_PAIR_INVALID, A_BOLD);
	field->attrs |= ATTR_READONLY;
	assert(!widget_is_interactive(field));
	assert(!widget_dispatch_input(field, L'y', false));
	widget_render_tree(field);
	check_style(field, A_UNDERLINE, COLOR_PAIR_READONLY);
	field->attrs &= ~ATTR_READONLY;
	field->attrs |= ATTR_DISABLED;
	assert(!widget_dispatch_input(field, L'\n', false));
	bool finished;
	assert(widget_get(field, PROP_INPUT_STATE, &finished) && !finished);
	widget_render_tree(field);
	check_style(field, A_DIM, COLOR_PAIR_DISABLED);
	field->attrs &= ~ATTR_DISABLED;
	field->flags |= FLAG_INFOCUS;
	int limit = 1;
	assert(widget_set(field, PROP_INPUT_MAX_LENGTH, &limit));
	assert(widget_dispatch_input(field, L'y', false));
	assert(field->flags & FLAG_REJECTED);
	const wchar_t *value;
	assert(widget_get(field, PROP_INPUT_VALUE, &value) && wcscmp(value, L"x") == 0);
	widget_render_tree(field);
	check_style(field, A_BOLD, COLOR_PAIR_INVALID);
	assert(widget_dispatch_input(field, KEY_HOME, true));
	assert(!(field->flags & FLAG_REJECTED));
	widget_render_tree(field);
	check_style(field, A_UNDERLINE | A_DIM, COLOR_PAIR_FOCUS);
	attr_t reverse = A_REVERSE;
	assert(widget_style_override(field, COLOR_PAIR_READONLY, NULL, NULL, &reverse, false));
	field->attrs |= ATTR_READONLY;
	widget_render_tree(field);
	check_style(field, A_REVERSE, COLOR_PAIR_READONLY);
	widget_free(field);

	struct widget *list = make_select(1, 2);
	assert(list);
	widget_add(list, make_select_option(L"One", true, true));
	widget_add(list, make_select_option(L"Two", false, true));
	int cursor = 1;
	assert(widget_set(list, PROP_SELECT_CURSOR, &cursor));
	assert(widget_dispatch_input(list, L' ', false));
	assert(list->flags & FLAG_REJECTED);
	bool selected;
	assert(widget_get_index(list, PROP_SELECT_OPTION_VALUE, 1, &selected) && !selected);
	widget_free(list);

	widget_free(border);
	widget_free(button);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	test_theme("linux", WIDGET_THEME_AUTO, COLOR_WHITE, COLOR_BLACK);
	test_theme("xterm-256color", WIDGET_THEME_AUTO, 236, 239);
	test_theme("xterm-256color", WIDGET_THEME_BASIC, COLOR_WHITE, COLOR_BLACK);
	test_theme("xterm", WIDGET_THEME_TERMINAL, -1, -1);
	test_theme("vt100", WIDGET_THEME_AUTO, 0, 0);
	return 0;
}
