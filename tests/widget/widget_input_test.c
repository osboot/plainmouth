// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <wchar.h>

#include <curses.h>

#include "widget.h"

static void expect_text(struct widget *w, const wchar_t *expected)
{
	wchar_t *text;
	assert(widget_get(w, PROP_INPUT_VALUE, &text));
	assert(wcscmp(text, expected) == 0);
}

static void test_enter(bool password)
{
	struct widget *w;
	if (password)
		w = make_input_password(L"a", NULL);
	else
		w = make_input(L"a", NULL);
	assert(w);
	bool finish;
	assert(widget_get(w, PROP_INPUT_FINISH_ON_ENTER, &finish));
	assert(finish);
	assert(w->ops->input(w, L'\n'));
	assert(widget_get(w, PROP_INPUT_STATE, &finish));
	assert(finish);
	assert(!w->ops->input(w, L'b'));
	expect_text(w, L"a");
	finish = false;
	assert(widget_set(w, PROP_INPUT_STATE, &finish));
	assert(widget_set(w, PROP_INPUT_FINISH_ON_ENTER, &finish));
	assert(w->ops->input(w, KEY_ENTER));
	assert(w->ops->input(w, L'b'));
	assert(w->ops->input(w, L'\n'));
	assert(w->ops->input(w, L'c'));
	assert(widget_get(w, PROP_INPUT_STATE, &finish));
	assert(!finish);
	expect_text(w, L"abc");
	widget_free(w);
}

static void test_limit(bool password)
{
	struct widget *w;
	if (password)
		w = make_input_password(L"ab", NULL);
	else
		w = make_input(L"ab", NULL);
	assert(w);
	int limit;
	assert(widget_get(w, PROP_INPUT_MAX_LENGTH, &limit));
	assert(limit == INT_MAX - 1);
	limit = 3;
	assert(widget_set(w, PROP_INPUT_MAX_LENGTH, &limit));
	assert(w->ops->input(w, L'c'));
	assert(!w->ops->input(w, L'd'));
	expect_text(w, L"abc");
	assert(!widget_set(w, PROP_INPUT_VALUE, L"long"));
	limit = 2;
	assert(!widget_set(w, PROP_INPUT_MAX_LENGTH, &limit));
	assert(widget_get(w, PROP_INPUT_MAX_LENGTH, &limit));
	assert(limit == 3);
	expect_text(w, L"abc");
	assert(w->ops->input(w, KEY_LEFT));
	assert(w->ops->input(w, KEY_BACKSPACE));
	assert(w->ops->input(w, L'x'));
	expect_text(w, L"axc");
	for (int i = 0; i < 3; i++)
		assert(w->ops->input(w, KEY_LEFT));
	assert(!w->ops->input(w, KEY_BACKSPACE));
	expect_text(w, L"axc");
	assert(widget_set(w, PROP_INPUT_VALUE, L"\u03b1\u03b2"));
	assert(w->ops->input(w, L'\u03b3'));
	assert(!w->ops->input(w, L'\u03b4'));
	expect_text(w, L"\u03b1\u03b2\u03b3");
	assert(widget_set(w, PROP_INPUT_VALUE, L""));
	limit = 0;
	assert(widget_set(w, PROP_INPUT_MAX_LENGTH, &limit));
	assert(!w->ops->input(w, L'a'));
	assert(!widget_set(w, PROP_INPUT_VALUE, L"a"));
	assert(widget_set(w, PROP_INPUT_VALUE, L""));
	limit = -1;
	assert(!widget_set(w, PROP_INPUT_MAX_LENGTH, &limit));
	limit = INT_MAX;
	assert(!widget_set(w, PROP_INPUT_MAX_LENGTH, &limit));
	expect_text(w, L"");
	widget_free(w);
}

static void test_edit(bool password)
{
	struct widget *w = password ? make_input_password(L"abc", NULL) : make_input(L"abc", NULL);
	assert(w);
	assert(w->ops->input_event(w, KEY_HOME, true));
	assert(w->ops->input_event(w, KEY_DC, true));
	expect_text(w, L"bc");
	assert(w->ops->input_event(w, L'X', false));
	assert(w->ops->input_event(w, KEY_END, true));
	assert(!w->ops->input_event(w, KEY_DC, true));
	assert(w->ops->input_event(w, L'Y', false));
	expect_text(w, L"XbcY");
	assert(w->ops->input_event(w, KEY_LEFT, true));
	assert(w->ops->input_event(w, KEY_DC, true));
	expect_text(w, L"Xbc");
	assert(widget_set(w, PROP_INPUT_VALUE, L"\u754c\u03b1"));
	assert(w->ops->input_event(w, KEY_HOME, true));
	assert(w->ops->input_event(w, KEY_DC, true));
	expect_text(w, L"\u03b1");
	widget_free(w);
}

static void test_viewport(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	struct widget *root = make_window(), *w = make_input(L"abcdef", NULL);
	assert(root && w);
	widget_add(root, w);
	widget_measure_tree(root);
	widget_layout_tree(root, 0, 0, 4, 1);
	widget_render_tree(root);
	assert((mvwinch(w->win, 0, 0) & A_CHARTEXT) == 'c');
	assert(w->ops->input_event(w, KEY_LEFT, true));
	widget_render_tree(w);
	assert((mvwinch(w->win, 0, 0) & A_CHARTEXT) == 'c');
	assert(w->ops->input_event(w, KEY_HOME, true));
	widget_render_tree(w);
	assert((mvwinch(w->win, 0, 0) & A_CHARTEXT) == 'a');
	assert(widget_set(w, PROP_INPUT_VALUE, L"\u754c\u03b1"));
	widget_render_tree(w);
	int y, x;
	getyx(w->win, y, x);
	assert(y == 0 && x == 3);
	widget_free(root);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	test_enter(false);
	test_enter(true);
	test_limit(false);
	test_limit(true);
	test_edit(false);
	test_edit(true);
	test_viewport();
	struct widget *w = make_input(L"", NULL);
	assert(w);
	assert(!w->ops->input_event(w, KEY_UP, true));
	assert(!w->ops->input_event(w, KEY_DOWN, true));
	assert(!w->ops->input_event(w, KEY_NPAGE, true));
	assert(!w->ops->input_event(w, KEY_F(1), true));
	expect_text(w, L"");
	/* The same numeric value is valid text when it is not a keycode. */
	assert(w->ops->input_event(w, KEY_UP, false));
	wchar_t value[] = { KEY_UP, L'\0' };
	expect_text(w, value);
	assert(w->ops->input_event(w, KEY_BACKSPACE, true));
	expect_text(w, L"");
	widget_free(w);
	return 0;
}
