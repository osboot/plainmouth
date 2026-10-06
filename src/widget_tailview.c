// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <wchar.h>

#include "widget.h"

struct tailview {
	wchar_t *text;
	int scroll_x;
};

static bool tailview_set(struct widget *w, enum widget_property prop, const void *value)
{
	struct tailview *st = w->state;
	if (prop == PROP_TEXT_VALUE) {
		wchar_t *copy = wcsdup(value);
		if (!copy)
			return false;
		free(st->text);
		st->text = copy;
		return true;
	}
	if (prop == PROP_SCROLL_X) {
		int x = *(const int *) value;
		if (x < 0)
			return false;
		st->scroll_x = x;
		return true;
	}
	return false;
}

static bool tailview_get(struct widget *w, enum widget_property prop, void *value)
{
	struct tailview *st = w->state;
	if (prop == PROP_TEXT_VALUE) {
		*(const wchar_t **) value = st->text;
		return true;
	}
	if (prop == PROP_SCROLL_X) {
		*(int *) value = st->scroll_x;
		return true;
	}
	return false;
}

static int tailview_input(const struct widget *w, wchar_t key)
{
	struct tailview *st = w->state;
	switch (key) {
		case KEY_LEFT:
		case L'h':
			if (st->scroll_x > 0)
				st->scroll_x--;
			break;
		case KEY_RIGHT:
		case L'l':
			if (st->scroll_x < INT_MAX - 8)
				st->scroll_x++;
			break;
		case L'0':
			st->scroll_x = 0;
			break;
		default:
			return 0;
	}
	return 1;
}

static void tailview_render(struct widget *w)
{
	struct tailview *st = w->state;
	const wchar_t *text = st->text;
	size_t lines = 0;
	for (const wchar_t *s = text; *s; s++)
		if (*s == L'\n' || !s[1])
			lines++;
	size_t skip = lines > (size_t) w->h ? lines - (size_t) w->h : 0;
	while (skip-- && *text) {
		const wchar_t *end = wcschr(text, L'\n');
		text = end ? end + 1 : text + wcslen(text);
	}
	widget_style_apply_widget(w, (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair);
	werase(w->win);
	for (int y = 0; y < w->h && *text; y++) {
		int col = 0;
		while (*text && *text != L'\n') {
			wchar_t ch = *text++;
			int width = ch == L'\t' ? 8 - col % 8 : wcwidth(ch);
			if (width < 0) {
				ch = L'?';
				width = 1;
			}
			int x = col - st->scroll_x;
			if (x >= 0 && x < w->w && width <= w->w - x && ch != L'\t')
				mvwaddnwstr(w->win, y, x, &ch, 1);
			col += width;
		}
		if (*text == L'\n')
			text++;
	}
}

static void tailview_free(struct widget *w)
{
	struct tailview *st = w->state;
	free(st->text);
	free(st);
}

static size_t tailview_keybindings(const struct widget *w, const struct widget_keybinding **bindings)
{
	(void) w;
	static const struct widget_keybinding keys[] = {
		{ KEY_LEFT,  true,  "Left",  "Scroll left"       },
		{ KEY_RIGHT, true,  "Right", "Scroll right"      },
		{ L'h',      false, "h",     "Scroll left"       },
		{ L'l',      false, "l",     "Scroll right"      },
		{ L'0',      false, "0",     "Beginning of line" },
	};
	*bindings = keys;
	return sizeof(keys) / sizeof(*keys);
}

static const struct widget_ops tailview_ops = {
	.keybindings = tailview_keybindings,
	.render = tailview_render,
	.free = tailview_free,
	.input = tailview_input,
	.setter = tailview_set,
	.getter = tailview_get,
};

struct widget *make_tailview(void)
{
	struct widget *w = widget_create(WIDGET_TAILVIEW);
	if (!w)
		return NULL;
	struct tailview *st = calloc(1, sizeof(*st));
	if (!st) {
		widget_free(w);
		return NULL;
	}
	st->text = wcsdup(L"");
	if (!st->text) {
		free(st);
		widget_free(w);
		return NULL;
	}
	w->state = st;
	w->ops = &tailview_ops;
	w->min_w = w->min_h = 1;
	w->flex_w = w->flex_h = 1;
	w->stretch_w = w->stretch_h = true;
	w->attrs = ATTR_CAN_FOCUS;
	w->color_pair = COLOR_PAIR_WINDOW;
	return w;
}
