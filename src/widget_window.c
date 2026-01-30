// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

static const struct widget_ops window_ops = {
	.measure          = vbox_measure,
	.layout           = vbox_layout,
	.render           = NULL,
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

struct widget *make_window(void)
{
	struct widget *w = widget_create(WIDGET_WINDOW);

	w->ops = &window_ops;

	w->min_w      = 10;
	w->min_h      = 5;
	w->color_pair = COLOR_PAIR_WINDOW;

	w->stretch_w = true;
	w->stretch_h = true;

	w->flex_w = 1;
	w->flex_h = 1;

	w->shrink_w = 1;
	w->shrink_h = 1;

	return w;
}
