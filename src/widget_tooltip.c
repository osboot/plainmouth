// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

struct widget_tooltip {
	wchar_t *text;
	bool clicked;

	struct widget *popup;
	PANEL *panel;
};

static void tooltip_measure(struct widget *w) __attribute__((nonnull(1)));
static void tooltip_render(struct widget *w) __attribute__((nonnull(1)));
static int tooltip_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static struct widget *make_popup(const wchar_t *desc, int y, int x) __attribute__((nonnull(1)));
static void tooltip_free(struct widget *w);


struct widget *make_popup(const wchar_t *desc, int y, int x)
{
	struct widget *root = make_window();
	struct widget *border = make_border_vbox(root);
	struct widget *text = make_textview(desc);

	widget_add(border, text);

	widget_measure_tree(root);
	widget_layout_tree(root, x, y, text->pref_w + 2, text->pref_h + 2);
	widget_render_tree(root);

	return root;
}

void tooltip_measure(struct widget *w)
{
	w->min_h = 1;
	w->min_w = 3; // "[?]"
}

void tooltip_render(struct widget *w)
{
	struct widget_tooltip *st = w->state;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	mvwaddwstr(w->win, 0, 0, L"[?]");
	wmove(w->win, 0, 0);

	if (st->clicked) {
		int y, x;
		widget_coordinates_yx(w, &y, &x);

		// Place popup under marker.
		y += 1;

		if (!st->panel) {
			st->popup = make_popup(st->text, y, x);
			st->panel = new_panel(st->popup->win);
		}
		show_panel(st->panel);
		top_panel(st->panel);

		st->clicked = false;

	} else if (st->popup) {
		hide_panel(st->panel);
	}
	update_panels();
}

void tooltip_free(struct widget *w)
{
	if (!w)
		return;

	struct widget_tooltip *st = w->state;

	if (st->panel)
		del_panel(st->panel);

	widget_free(st->popup);

	free(st->text);
	free(st);
}

int tooltip_input(const struct widget *w, wchar_t key)
{
	struct widget_tooltip *st = w->state;

	switch (key) {
		case KEY_ENTER:
		case L' ':
		case L'\n':
			st->clicked = true;
			break;
		default:
			st->clicked = false;
			break;
	}

	return 1;
}

static const struct widget_ops tooltip_ops = {
	.measure          = tooltip_measure,
	.layout           = NULL,
	.render           = tooltip_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = tooltip_free,
	.input            = tooltip_input,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = NULL,
	.getter           = NULL,
	.getter_index     = NULL,
};

struct widget *make_tooltip(const wchar_t *line)
{
	struct widget *w = widget_create(WIDGET_TOOLTIP);

	if (!w)
		return NULL;

	struct widget_tooltip *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_tooltip: calloc");
		widget_free(w);
		return NULL;
	}

	state->text = wcsdup(line ?: L"");

	w->state = state;
	w->ops = &tooltip_ops;
	w->color_pair    = COLOR_PAIR_WINDOW;
	w->attrs         = ATTR_CAN_FOCUS;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false; /* do not expand horizontally */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
