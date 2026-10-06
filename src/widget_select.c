// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <time.h>
#include <wctype.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget_select {
	int max_selected;
	int selected;
	bool menu;
	bool finished;
	struct widget *focus;
	struct widget *list;
	struct widget *vscroll;
	wchar_t prefix[128];
	size_t prefix_len;
	struct timespec search_time;
};

static void select_sync(struct widget *w) __attribute__((nonnull(1)));
static void select_measure(struct widget *w) __attribute__((nonnull(1)));
static void select_layout(struct widget *w) __attribute__((nonnull(1)));
static void select_render(struct widget *w) __attribute__((nonnull(1)));
static void select_ensure_visible(struct widget *w, struct widget *child) __attribute__((nonnull(1, 2)));
static int select_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool select_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1, 3)));
static bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *value) __attribute__((nonnull(1, 4)));
static bool select_setter_index(struct widget *w, enum widget_property prop, int index, const void *value) __attribute__((nonnull(1, 4)));
static bool select_setter(struct widget *w, enum widget_property prop, const void *value);
static void select_add_child(struct widget *sv, struct widget *child) __attribute__((nonnull(1, 2)));
static void select_free(struct widget *w);

void select_sync(struct widget *sv)
{
	struct widget_select *st = sv->state;

	widget_sync_vscroll(st->list, st->vscroll);
	if (st->vscroll->win)
		widget_render_tree(st->vscroll);
}

void select_measure(struct widget *w)
{
	struct widget_select *st = w->state;

	st->list->ops->measure(st->list);

	w->min_h = 1;
	w->pref_h = st->list->pref_h;

	w->min_w = 1;
	w->pref_w = st->list->pref_w + 1;
}

void select_layout(struct widget *w)
{
	struct widget_select *st = w->state;
	struct widget *hbox = TAILQ_FIRST(&w->children);

	widget_layout_tree(hbox, 0, 0, w->w, w->h);

	struct widget *option;
	TAILQ_FOREACH(option, &st->list->children, siblings) {
		if (option->flags & FLAG_INFOCUS) {
			st->list->ops->ensure_visible(st->list, option);
			break;
		}
	}
}

int widget_select_search_timeout(struct widget *w)
{
	if (!w || w->type != WIDGET_SELECT)
		return -1;

	struct widget_select *st = w->state;

	if (!st->prefix_len)
		return -1;

	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;

	long long ns = (st->search_time.tv_sec - now.tv_sec + 1) * 1000000000LL +
		       st->search_time.tv_nsec - now.tv_nsec;
	if (ns <= 0)
		return 0;

	return (int) ((ns + 999999) / 1000000);
}

void select_render(struct widget *w)
{
	struct widget_select *st = w->state;

	if (!(w->flags & FLAG_INFOCUS))
		st->prefix_len = 0;

	if (widget_select_search_timeout(w) == 0)
		st->prefix_len = 0;

	struct widget *option;
	TAILQ_FOREACH(option, &st->list->children, siblings)
	{
		int length = 0;
		if (option == st->focus)
			length = (int) st->prefix_len;
		widget_set(option, PROP_TEXT_PREFIX_LENGTH, &length);
	}

	if (w->flags & FLAG_INFOCUS)
		st->vscroll->flags |= FLAG_INFOCUS;
	else
		st->vscroll->flags &= ~FLAG_INFOCUS;

	select_sync(w);
}

void select_ensure_visible(struct widget *w, struct widget *child)
{
	struct widget_select *st = w->state;

	st->list->ops->ensure_visible(st->list, child);

	select_sync(w);
}

static int select_search(const struct widget *w, wchar_t key)
{
	struct widget_select *st = w->state;

	if (!iswprint((wint_t) key) || !st->focus)
		return 0;

	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return 0;

	if (now.tv_sec - st->search_time.tv_sec > 1 ||
	    (now.tv_sec - st->search_time.tv_sec == 1 && now.tv_nsec >= st->search_time.tv_nsec))
		st->prefix_len = 0;

	st->search_time = now;

	bool cycle = st->prefix_len == 1 && (wint_t) st->prefix[0] == towlower((wint_t) key);

	if (!cycle) {
		if (st->prefix_len == 127)
			return 0;
		st->prefix[st->prefix_len++] = (wchar_t) towlower((wint_t) key);
	}

	int cursor = 0, size = 0;
	select_getter((struct widget *) w, PROP_SELECT_CURSOR, &cursor);
	select_getter((struct widget *) w, PROP_SELECT_OPTIONS_SIZE, &size);

	for (int pass = 0; pass < 2; pass++) {
		int best = -1, best_distance = size;
		int index = 0;

		struct widget *option;
		TAILQ_FOREACH(option, &st->list->children, siblings)
		{
			const wchar_t *text = NULL;
			if (widget_get(option, PROP_TEXT_VALUE, &text) && text) {
				size_t n = 0;
				while (n < st->prefix_len && text[n] &&
				       towlower((wint_t) text[n]) == (wint_t) st->prefix[n])
					n++;
				int start = cursor;
				if (cycle || st->prefix_len == 1)
					start++;
				int distance = (index - start + size) % size;
				if (n == st->prefix_len && distance < best_distance) {
					best = index;
					best_distance = distance;
				}
			}
			index++;
		}

		if (best >= 0) {
			size_t length = st->prefix_len;
			bool ok = select_setter((struct widget *) w, PROP_SELECT_CURSOR, &best);
			if (ok)
				st->prefix_len = length;
			return ok;
		}

		st->prefix_len = 1;
		st->prefix[0] = (wchar_t) towlower((wint_t) key);
	}

	st->prefix_len = 0;

	return 0;
}

int select_input(const struct widget *w, wchar_t key)
{
	struct widget_select *st = w->state;

	if (key == L'\n' || key == KEY_ENTER) {
		if (!st->menu)
			return 0;
		st->finished = st->focus != NULL;
		return 1;
	}
	if (!st->focus)
		return 0;
	{
		bool navigate = true;
		int index = 0, size = 0;
		select_getter((struct widget *) w, PROP_SELECT_CURSOR, &index);
		select_getter((struct widget *) w, PROP_SELECT_OPTIONS_SIZE, &size);
		switch (key) {
			case KEY_UP:
				index--;
				break;
			case KEY_DOWN:
				index++;
				break;
			case KEY_PPAGE:
				index -= w->h;
				break;
			case KEY_NPAGE:
				index += w->h;
				break;
			case KEY_HOME:
				index = 0;
				break;
			case KEY_END:
				index = size - 1;
				break;
			case L' ':
				navigate = false;
				break;
			default:
				return select_search(w, key);
		}
		st->prefix_len = 0;
		if (navigate) {
			index = MAX(0, MIN(index, size - 1));
			select_setter((struct widget *) w, PROP_SELECT_CURSOR, &index);
			return 1;
		}
	}

	switch (key) {
		case L' ':
			if (st->menu)
				return 1;
			if (st->focus) {
				bool clicked = false;

				widget_get(st->focus, PROP_CHECKBOX_STATE, &clicked);

				if (clicked)
					st->selected--;
				else if (st->selected < st->max_selected)
					st->selected++;
				else
					return -1;

				clicked = !clicked;
				widget_set(st->focus, PROP_CHECKBOX_STATE, &clicked);
			}
			break;

		default:
			return 0;
	}
	return 1;
}

static int select_input_event(const struct widget *w, wchar_t key, bool keycode)
{
	if (!keycode) {
		if (key == L' ' || key == L'\n')
			return select_input(w, key);
		return select_search(w, key);
	}
	switch (key) {
		case KEY_UP:
		case KEY_DOWN:
		case KEY_HOME:
		case KEY_END:
		case KEY_PPAGE:
		case KEY_NPAGE:
		case KEY_ENTER:
			return select_input(w, key);
		default:
			return 0;
	}
}

bool select_getter(struct widget *w, enum widget_property prop, void *value)
{
	struct widget_select *st = w->state;

	if (prop == PROP_SELECT_STATE) {
		*(bool *) value = st->finished;
		return true;
	}

	if (prop == PROP_SELECT_OPTIONS_SIZE) {
		int size = 0;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings)
		{
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			size++;
		}
		*(int *) value = size;
		return true;
	}

	if (prop == PROP_SELECT_CURSOR) {
		int index = 0;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings)
		{
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			if (c == st->focus)
				break;

			index++;
		}
		*(int *) value = index;
		return true;
	}

	return false;
}

bool select_getter_index(struct widget *w, enum widget_property prop, int index, void *value)
{
	struct widget_select *st = w->state;

	if (prop == PROP_SELECT_OPTION_VALUE) {
		int i = 0;
		bool clicked = false;

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings)
		{
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			if (i == index) {
				widget_get(c, PROP_CHECKBOX_STATE, &clicked);
				break;
			}
			i++;
		}

		*(bool *) value = clicked;
		return true;
	}

	return false;
}

bool select_setter_index(struct widget *w, enum widget_property prop, int index, const void *value)
{
	struct widget_select *st = w->state;

	if (prop == PROP_SELECT_OPTION_VALUE) {
		int i = 0;
		bool selected = !!(*(const bool *) value);

		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings)
		{
			if (c->type != WIDGET_SELECT_OPT)
				continue;

			if (i == index) {
				bool current = false;
				widget_get(c, PROP_CHECKBOX_STATE, &current);

				if (current == selected)
					return true;

				if (selected && st->max_selected <= 1) {
					struct widget *other;
					TAILQ_FOREACH(other, &st->list->children, siblings)
					{
						if (other->type == WIDGET_SELECT_OPT) {
							bool off = false;
							widget_set(other, PROP_CHECKBOX_STATE, &off);
						}
					}
					st->selected = 0;
				}

				if (selected && st->selected >= st->max_selected)
					return false;

				widget_set(c, PROP_CHECKBOX_STATE, &selected);
				st->selected += selected ? 1 : -1;
				st->list->ops->ensure_visible(st->list, c);
				return true;
			}
			i++;
		}
	}

	return false;
}

static bool select_setter(struct widget *w, enum widget_property prop, const void *value)
{
	struct widget_select *st = w->state;
	if (prop == PROP_SELECT_STATE) {
		if (!st->menu)
			return false;
		st->finished = *(const bool *) value;
		return true;
	}
	if (prop == PROP_SELECT_CURSOR) {
		int index = *(const int *) value;
		int i = 0;
		struct widget *c;
		TAILQ_FOREACH(c, &st->list->children, siblings)
		{
			if (c->type != WIDGET_SELECT_OPT)
				continue;
			if (i++ != index)
				continue;
			if (st->focus)
				st->focus->flags &= ~FLAG_INFOCUS;
			st->focus = c;
			st->prefix_len = 0;
			c->flags |= FLAG_INFOCUS;
			st->list->ops->ensure_visible(st->list, c);
			return true;
		}
	}
	return false;
}

void select_add_child(struct widget *sv, struct widget *child)
{
	struct widget_select *st = sv->state;

	if (!st->focus) {
		st->focus = child;
		child->flags |= FLAG_INFOCUS;
	}

	child->attrs &= ~ATTR_CAN_FOCUS;
	widget_add(st->list, child);
	bool checked = false;
	if (widget_get(child, PROP_CHECKBOX_STATE, &checked) && checked)
		st->selected++;
}

void select_free(struct widget *w)
{
	if (!w)
		return;
	free(w->state);
}

static size_t select_keybindings(const struct widget *w, const struct widget_keybinding **bindings)
{
	const struct widget_select *st = w->state;
	static const struct widget_keybinding menu_keys[] = {
		{ KEY_UP,    true,  "Up",     "Previous item"        },
		{ KEY_DOWN,  true,  "Down",   "Next item"            },
		{ KEY_PPAGE, true,  "PgUp",   "Previous page"        },
		{ KEY_NPAGE, true,  "PgDown", "Next page"            },
		{ KEY_HOME,  true,  "Home",   "First item"           },
		{ KEY_END,   true,  "End",    "Last item"            },
		{ 0,         false, "Text",   "Search by prefix"     },
		{ KEY_ENTER, true,  "Enter",  "Accept selected item" },
	};
	static const struct widget_keybinding select_keys[] = {
		{ KEY_UP,    true,  "Up",     "Previous item"        },
		{ KEY_DOWN,  true,  "Down",   "Next item"            },
		{ KEY_PPAGE, true,  "PgUp",   "Previous page"        },
		{ KEY_NPAGE, true,  "PgDown", "Next page"            },
		{ KEY_HOME,  true,  "Home",   "First item"           },
		{ KEY_END,   true,  "End",    "Last item"            },
		{ 0,         false, "Text",   "Search by prefix"     },
		{ L' ',      false, "Space",  "Toggle selected item" },
	};
	if (!st->focus)
		return 0;
	if (st->menu) {
		*bindings = menu_keys;
		return sizeof(menu_keys) / sizeof(*menu_keys);
	}
	*bindings = select_keys;
	return sizeof(select_keys) / sizeof(*select_keys);
}

static const struct widget_ops select_ops = {
	.keybindings      = select_keybindings,
	.measure          = select_measure,
	.layout           = select_layout,
	.render           = select_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free             = select_free,
	.input            = select_input,
	.input_event      = select_input_event,
	.add_child        = select_add_child,
	.ensure_visible   = select_ensure_visible,
	.setter           = select_setter,
	.getter           = select_getter,
	.getter_index     = select_getter_index,
	.setter_index     = select_setter_index,
};

struct widget *make_select(int max_selected, int view_rows)
{
	struct widget *root = widget_create(WIDGET_SELECT);
	struct widget *hbox = make_hbox();
	struct widget *list = make_list_vbox(view_rows);
	struct widget *vs = make_vscroll();

	if (!root || !hbox || !list || !vs) {
		goto fail;
	}

	struct widget_select *st = calloc(1, sizeof(*st));
	if (!st) {
		warn("make_select_box: calloc");
		goto fail;
	}

	st->max_selected = max_selected;
	st->list = list;
	st->vscroll = vs;

	root->state = st;

	widget_add(root, hbox);
	widget_add(hbox, list);
	widget_add(hbox, vs);

	root->ops = &select_ops;
	root->color_pair = COLOR_PAIR_WINDOW;
	root->attrs = ATTR_CAN_FOCUS;

	root->stretch_w = true;
	root->stretch_h = true;

	root->flex_w = 1;
	root->flex_h = 1;

	return root;
fail:
	widget_free(list);
	widget_free(vs);
	widget_free(hbox);
	widget_free(root);

	return NULL;
}

struct widget *make_menu(int view_rows)
{
	struct widget *w = make_select(1, view_rows);
	if (w)
		((struct widget_select *) w->state)->menu = true;
	return w;
}
