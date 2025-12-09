// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <err.h>

#include "macros.h"
#include "widget.h"

struct widget *make_window(void)
{
	struct widget *w = widget_create(WIDGET_WINDOW);

	w->measure    = vbox_measure;
	w->layout     = vbox_layout;
	w->render     = NULL; /* could draw border */
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
