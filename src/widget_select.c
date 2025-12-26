// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <wchar.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "warray.h"
#include "widget.h"

struct widget_select {
	struct warray items;

	bool *selected;
	int max_selected;
	int nselected;

	int cursor;
	int vscroll;

	int view_rows;
};

static void select_measure(struct widget *w) __attribute__((nonnull(1)));
static void select_render(struct widget *w) __attribute__((nonnull(1)));
static void select_free(struct widget *w) __attribute__((nonnull(1)));
static int select_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool select_getter(struct widget *w, enum widget_property prop, void *out) __attribute__((nonnull(1,3)));
static bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *out) __attribute__((nonnull(1,4)));


void select_measure(struct widget *w)
{
        struct widget_select *s = w->state.select;

        int maxlen = 0;
        for (size_t i = 0; i < s->items.size; i++) {
                const wchar_t *line = warray_get(&s->items, i);
                if (line)
                        maxlen = MAX(maxlen, (int) wcslen(line));
        }

        w->min_h  = 1;
        w->min_w  = maxlen + 4; // "[x] " + text

        w->pref_h = s->view_rows ? s->view_rows : MIN(5, (int) s->items.size);
        w->pref_w = w->min_w;
}

void select_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	struct widget_select *s = w->state.select;
	int height, width;

	getmaxyx(w->win, height, width);
	werase(w->win);

	int items_size = (int) s->items.size;
	int last = MIN(items_size, s->vscroll + height);

	for (int i = s->vscroll, row = 0; i < last; i++, row++) {
		const wchar_t *item = warray_get(&s->items, (size_t) i);

		if (i == s->cursor)
			wattron(w->win, A_REVERSE);

		if (item) {
			char buf[5];

			snprintf(buf, sizeof(buf),
					(s->max_selected > 1 ? "[%c] " : "(%c) "),
					(s->selected[i] ? '*' : ' '));

			wmove(w->win, row, 0);
			waddnstr(w->win, buf, sizeof(buf) - 1);
			waddnwstr(w->win, item, width - 4);
		}

		if (i == s->cursor) {
			for (int j = 4 + (int) wcslen(item); j < width; j++)
				waddch(w->win, ' ');

			wattroff(w->win, A_REVERSE);
		}
	}

	/* scrollbar */
	if (items_size > height)
		widget_draw_vscroll(w->win, color, s->vscroll, items_size);

	wnoutrefresh(w->win);
}

void select_free(struct widget *w)
{
	if (w->state.select) {
		warray_free(&w->state.select->items);
		free(w->state.select->selected);
		free(w->state.select);
	}
}

int select_input(const struct widget *w, wchar_t key)
{
	struct widget_select *s = w->state.select;
	int count = (int) s->items.size;

	switch (key) {
		case KEY_UP:
			if (s->cursor > 0)
				s->cursor--;
			break;

		case KEY_DOWN:
			if (s->cursor < count - 1)
				s->cursor++;
			break;

		case KEY_PPAGE:
			s->cursor = MAX(0, s->cursor - w->h);
			break;

		case KEY_NPAGE:
			s->cursor = MIN(count - 1, s->cursor + w->h);
			break;

		case L' ':
			if (s->selected[s->cursor]) {
				s->selected[s->cursor] = false;
				s->nselected--;

			} else if (s->max_selected == 1) {
				memset(s->selected, 0, sizeof(bool) * (size_t) count);

				s->selected[s->cursor] = true;
				s->nselected = 1;

			} else if (s->nselected < s->max_selected) {
				if (s->max_selected == 1) {
					memset(s->selected, 0, sizeof(bool) * (size_t) count);
					s->nselected = 0;
				}
				s->selected[s->cursor] = true;
				s->nselected++;
			}
			break;

		default:
			return 0;
	}

	if (s->cursor < s->vscroll)
		s->vscroll = s->cursor;

	else if (s->cursor >= s->vscroll + w->h)
		s->vscroll = s->cursor - w->h + 1;

	return 1;
}

bool select_getter(struct widget *w, enum widget_property prop, void *out)
{
	struct widget_select *s = w->state.select;

	switch (prop) {
		case PROP_SELECT_OPTIONS_SIZE:
			*(int *) out = (int) s->items.size;
			return true;
		case PROP_SELECT_CURSOR:
			*(int *) out = s->cursor;
			return true;
		default:
			break;
	}
	return false;
}

bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *out)
{
	struct widget_select *s = w->state.select;

	switch (prop) {
		case PROP_SELECT_OPTION_VALUE:
			if (index >= 0 && index < (int) s->items.size) {
				*(bool *) out = s->selected[index];
				return true;
			}
			break;
		default:
			break;
	}
	return false;
}

struct widget *make_select(int max_selected, int view_rows)
{
	struct widget *w = widget_create(WIDGET_SELECT);
	if (!w)
		return NULL;

	struct widget_select *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("make_select: calloc");
		widget_free(w);
		return NULL;
	}

	warray_init(&state->items);

	state->max_selected = MAX(1, max_selected);
	state->view_rows = view_rows;

	w->state.select = state;
	w->measure      = select_measure;
	w->render       = select_render;
	w->input        = select_input;
	w->free_data    = select_free;
	w->getter       = select_getter;
	w->getter_index = select_getter_index;
	w->color_pair   = COLOR_PAIR_WINDOW;
	w->attrs        = ATTR_CAN_FOCUS;

	w->flex_w = 1;
	w->flex_h = 1;
	w->stretch_w = true;
	w->stretch_h = true;

	return w;
}

bool make_select_option(struct widget *w, const wchar_t *item)
{
	bool *selected;
	struct widget_select *s;

	if (!w || !w->state.select) {
		warnx("bad select widget");
		return false;
	}

	s = w->state.select;

	selected = realloc(s->selected, sizeof(bool) * (s->items.size + 1));
	if (!selected) {
		warn("unable to extend selected");
		return false;
	}
	selected[s->items.size] = false;

	if (warray_push(&s->items, item, 0) < 0) {
		warnx("unable to push new item");
		free(selected);
		return false;
	}

	s->selected = selected;

	return true;
}
