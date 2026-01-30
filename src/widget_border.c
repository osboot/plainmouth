// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The border implementation is intentionally designed as a separate widget.
 * There is not much space on the screen, even when using a framebuffer. When a
 * plugin decides to surround certain elements with a border, it is better to
 * see this explicitly.
 */
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

static void border_measure(struct widget *w) __attribute__((nonnull(1)));
static void border_layout(struct widget *w) __attribute__((nonnull(1)));
static void border_render(struct widget *w) __attribute__((nonnull(1)));

void border_measure(struct widget *w)
{
	int max_w = 2;
	int max_h = 2;

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		max_w = MAX(max_w, c->min_w + 2);
		max_h = MAX(max_h, c->min_h + 2);
	}

	w->min_w = max_w;
	w->min_h = max_h;
}

void border_layout(struct widget *w)
{
	int inner_w = MAX(0, w->w - 2);
	int inner_h = MAX(0, w->h - 2);

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		widget_layout_tree(c, 1, 1, inner_w, inner_h);
	}
}

void border_render(struct widget *w)
{
	box(w->win, 0, 0);
}

static const struct widget_ops border_ops = {
	.measure          = border_measure,
	.layout           = border_layout,
	.render           = border_render,
	.finalize_render  = NULL,
	.child_render_win = NULL,
	.free_data        = NULL,
	.input            = NULL,
	.add_child        = NULL,
	.ensure_visible   = NULL,
	.setter           = NULL,
	.getter           = NULL,
	.getter_index     = NULL,
};

struct widget *make_border(void)
{
	struct widget *w = widget_create(WIDGET_BORDER);
	if (!w)
		return NULL;

	w->ops = &border_ops;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->flex_h = 1;
	w->flex_w = 1;
	w->stretch_w = true;
	w->stretch_h = true;

	return w;
}

struct widget *make_border_vbox(struct widget *parent)
{
	struct widget *vbox = make_vbox();
	struct widget *border = make_border();

	if (!vbox || !border)
		goto failure;

	widget_add(border, vbox);
	widget_add(parent, border);

	return vbox;
failure:
	widget_free(border);
	widget_free(vbox);

	return NULL;
}

struct widget *make_border_hbox(struct widget *parent)
{
	struct widget *hbox = make_hbox();
	struct widget *border = make_border();

	if (!hbox || !border)
		goto failure;

	widget_add(border, hbox);
	widget_add(parent, border);

	return hbox;
failure:
	widget_free(border);
	widget_free(hbox);

	return NULL;
}
