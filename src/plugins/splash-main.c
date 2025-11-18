// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <curses.h>
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

	mvwprintw(win, 2, 1 + mid, "[%3d%%]", simple_round(((float) splash->value * 100) / (float) splash->total));
}

static PANEL *p_splash_create(struct request *req)
{
	PANEL *panel;
	chtype bdr[BORDER_SIZE];
	struct splash *splash;

	splash = calloc(1, sizeof(*splash));
	if (!splash) {
		warn("calloc splash");
		return NULL;
	}

	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int ncols = req_get_int(req, "width", -1);
	int nlines = 1;

	if (ncols < 0)
		ncols = COLS - 4;

	splash->width = ncols;
	splash->percent = req_get_bool(req, "percent", false);
	splash->total = req_get_int(req, "total", 0);
	splash->borders = widget_borders(req, bdr);

	if (splash->borders) {
		ncols  += 2;
		nlines += 2;
	}

	position_center(ncols, nlines, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);
	wbkgd(win, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (splash->borders) {
		wborder(win,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);
	}

	panel = new_panel(win);
	set_panel_userptr(panel, splash);

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

	int value = req_get_int(req, "value", 0);

	if (value < 0)
		value = 0;

	if (value > splash->total)
		value = splash->total;

	splash->value = value;

	int num = simple_round(((float) splash->value / (float) splash->total) * (float) splash->width);
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

static bool p_splash_finished(PANEL *panel)
{
	const struct splash *splash = panel_userptr(panel);

	return splash->value == splash->total;
}

struct plugin plugin = {
	.name              = "splash",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_splash_create,
	.p_delete_instance = p_splash_delete,
	.p_update_instance = p_splash_update,
	.p_finished        = p_splash_finished,
	.p_input           = NULL,
	.p_result          = NULL,
	.p_get_cursor      = NULL,
};
