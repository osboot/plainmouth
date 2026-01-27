// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

static void selopt_measure(struct widget *w) __attribute__((nonnull(1)));
static void selopt_layout(struct widget *w) __attribute__((nonnull(1)));
static void selopt_render(struct widget *w) __attribute__((nonnull(1)));
static int selopt_input(const struct widget *w, wchar_t key) __attribute__((nonnull(1)));
static bool selopt_getter(struct widget *w, enum widget_property prop, void *value) __attribute__((nonnull(1,3)));
static bool selopt_setter(struct widget *w, enum widget_property prop, const void *value) __attribute__((nonnull(1,3)));


void selopt_measure(struct widget *w)
{
	w->min_w = w->min_h = 0;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		w->min_w += c->min_w;
		w->min_h = MAX(w->min_h, c->min_h);
	}
}

void selopt_layout(struct widget *w)
{
	struct widget *c;
	int i, x;

	x = i = 0;
	TAILQ_FOREACH(c, &w->children, siblings) {
		int cw = (c->pref_w > 0) ? c->pref_w : c->min_w;
		int ch = (c->pref_h > 0) ? c->pref_h : c->min_h;
		widget_layout_tree(c, x, 0, cw, ch);
		x += cw;
		i++;
	}
}

void selopt_render(struct widget *w)
{
	enum color_pair color = (w->flags & FLAG_INFOCUS) ? COLOR_PAIR_FOCUS : w->color_pair;
	wbkgd(w->win, COLOR_PAIR(color));

	struct widget *hbox = TAILQ_FIRST(&w->children);

	struct widget *c;
	TAILQ_FOREACH(c, &hbox->children, siblings) {
		if (w->flags & FLAG_INFOCUS)
			c->flags |= FLAG_INFOCUS;
		else
			c->flags &= ~FLAG_INFOCUS;
	}
	widget_render_tree(hbox);
}

int selopt_input(const struct widget *w, wchar_t key)
{
	struct widget *hbox = TAILQ_FIRST(&w->children);

	struct widget *c;
	TAILQ_FOREACH(c, &hbox->children, siblings) {
		if (c->type == WIDGET_CHECKBOX && c->input) {
			return c->input(c, key);
		}
	}
	return 0;
}

bool selopt_getter(struct widget *w, enum widget_property prop, void *value)
{
	if (prop == PROP_CHECKBOX_STATE) {
		struct widget *hbox = TAILQ_FIRST(&w->children);

		struct widget *c;
		TAILQ_FOREACH(c, &hbox->children, siblings) {
			if (c->type == WIDGET_CHECKBOX && c->getter) {
				return c->getter(c, prop, value);
			}
		}
	}
	return false;
}

bool selopt_setter(struct widget *w, enum widget_property prop, const void *value)
{
	if (prop == PROP_CHECKBOX_STATE) {
		struct widget *hbox = TAILQ_FIRST(&w->children);

		struct widget *c;
		TAILQ_FOREACH(c, &hbox->children, siblings) {
			if (c->type == WIDGET_CHECKBOX && c->getter) {
				return c->setter(c, prop, value);
			}
		}
	}
	return false;
}

struct widget *make_select_option(const wchar_t *text, bool checked, bool is_radio)
{
	struct widget *w = widget_create(WIDGET_SELECT_OPT);
	struct widget *hbox = make_hbox();
	struct widget *checkbox = make_checkbox(checked, is_radio);
	struct widget *label = make_label(text);

	if (!w || !hbox || !checkbox || !label) {
		widget_free(hbox);
		widget_free(checkbox);
		widget_free(label);
		widget_free(w);
		return NULL;
	}

	label->attrs    &= ~ATTR_CAN_FOCUS;
	checkbox->attrs &= ~ATTR_CAN_FOCUS;

	widget_add(hbox, checkbox);
	widget_add(hbox, label);
	widget_add(w, hbox);

	w->measure    = selopt_measure;
	w->layout     = selopt_layout;
	w->render     = selopt_render;
	w->input      = selopt_input;
	w->getter     = selopt_getter;
	w->setter     = selopt_setter;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->attrs |= ATTR_CAN_FOCUS;

	w->flex_h = 1;
	w->flex_w = 1;

	w->stretch_w = true;

	return w;
}
