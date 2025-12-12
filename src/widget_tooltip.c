// SPDX-License-Identifier: GPL-2.0-or-later

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

static void tooltip_measure(struct widget *w);
static void tooltip_render(struct widget *w);
static void tooltip_free(struct widget *w);
static int tooltip_input(const struct widget *w, wchar_t key);
static struct widget *make_popup(const wchar_t *desc, int y, int x);


struct widget *make_popup(const wchar_t *desc, int y, int x)
{
	struct widget *root = make_window();
	struct widget *border = make_border_vbox(root);
	struct widget *text = make_textview(desc);

	widget_add(border, text);

	widget_measure_tree(root);
	widget_layout_tree(root, x, y, text->pref_w + 2, text->pref_h + 2);
	widget_create_tree(root);
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
	struct widget_tooltip *state = w->state.tooltip;

	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	mvwaddwstr(w->win, 0, 0, L"[?]");
	wmove(w->win, 0, 0);

	if (state->clicked) {
		int y, x;
		widget_coordinates_yx(w, &y, &x);

		// Place popup under marker.
		y += 1;

		if (!state->panel) {
			state->popup = make_popup(state->text, y, x);
			state->panel = new_panel(state->popup->win);
		}
		show_panel(state->panel);
		top_panel(state->panel);

		state->clicked = false;

	} else if (state->popup) {
		hide_panel(state->panel);
	}
	update_panels();
}

void tooltip_free(struct widget *w)
{
	if (!w->state.tooltip)
		return;

	if (w->state.tooltip->panel)
		del_panel(w->state.tooltip->panel);

	widget_free(w->state.tooltip->popup);

	free(w->state.tooltip->text);
	free(w->state.tooltip);
}

int tooltip_input(const struct widget *w, wchar_t key)
{
	struct widget_tooltip *state = w->state.tooltip;

	switch (key) {
		case KEY_ENTER:
		case L' ':
		case L'\n':
			state->clicked = true;
			break;
		default:
			state->clicked = false;
			break;
	}

	return 1;
}

struct widget *make_tooltip(const wchar_t *line)
{
	struct widget_tooltip *state = NULL;
	struct widget *w = widget_create(WIDGET_TOOLTIP);

	if (!w)
		return NULL;

	state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_tooltip: calloc");
		widget_free(w);
		return NULL;
	}

	state->text = wcsdup(line ?: L"");

	w->state.tooltip = state;
	w->color_pair    = COLOR_PAIR_WINDOW;
	w->measure       = tooltip_measure;
	w->render        = tooltip_render;
	w->free_data     = tooltip_free;
	w->input         = tooltip_input;
	w->attrs         = ATTR_CAN_FOCUS;

	w->flex_w = 0;
	w->flex_h = 0;
	w->stretch_w = false; /* do not expand horizontally */
	w->stretch_h = false; /* keep 1 line */

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
