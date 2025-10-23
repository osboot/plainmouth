// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <ncurses.h>
#include <panel.h>

#include "helpers.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

struct splash {
	bool percent;
	bool borders;
	int width;
	int total;
	int value;
};

static void show_percent(PANEL *panel)
{
	const struct splash *splash = panel_userptr(panel);

	if (!splash->percent || !splash->borders || splash->width < 6)
		return;

	WINDOW *win = panel_window(panel);
	int mid = (splash->width / 2) - 3;

	mvwprintw(win, 2, 1 + mid, "[%3d%%]", widget_round(((float) splash->value * 100) / (float) splash->total));
}

static PANEL *p_splash_create(struct request *req)
{
	PANEL *panel;
	struct splash *splash;

	splash = calloc(1, sizeof(*splash));
	if (!splash) {
		warn("calloc splash");
		return NULL;
	}

	int begin_x = ipc_get_int(req_data(req), "x", -1);
	int begin_y = ipc_get_int(req_data(req), "y", -1);

	splash->percent = ipc_get_bool(req_data(req), "percent", false);
	splash->borders = ipc_get_bool(req_data(req), "borders", false);
	splash->total = ipc_get_int(req_data(req), "total", 0);

	int border_cols = (splash->borders ? 2 : 0);

	splash->width = COLS - border_cols - 2;

	int nlines = 1 + border_cols;
	int ncols = splash->width + border_cols;

	widget_begin_yx(ncols, splash->borders, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);
	panel = new_panel(win);

	set_panel_userptr(panel, splash);

	if (splash->borders)
		box(win, 0, 0);

	show_percent(panel);

	return panel;
}

static enum p_retcode p_splash_delete(PANEL *panel)
{
	struct splash *splash = (struct splash *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	del_panel(panel);
	delwin(win);
	free(splash);

	return P_RET_OK;
}

static enum p_retcode p_splash_update(struct request *req, PANEL *panel)
{
	struct splash *splash = (struct splash *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	int value = ipc_get_int(req_data(req), "value", 0);

	if (value < 0)
		value = 0;

	if (value > splash->total)
		value = splash->total;

	splash->value = value;

	int num = widget_round(((float) splash->value / (float) splash->total) * (float) splash->width);
	int i = (splash->borders ? 1 : 0);

	wattron(win, A_REVERSE);

	for (; i <= num; i++)
		mvwaddch(win, (splash->borders ? 1 : 0), i, ' ');

	wattroff(win, A_REVERSE);

	for (; i <= splash->width; i++)
		mvwaddch(win, (splash->borders ? 1 : 0), i, ' ');

	show_percent(panel);

	return P_RET_OK;
}

struct plugin plugin = {
	.name              = "splash",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_widget   = p_splash_create,
	.p_delete_widget   = p_splash_delete,
	.p_update_widget   = p_splash_update,
	.p_finished        = NULL,
	.p_input           = NULL,
	.p_result          = NULL,
};
