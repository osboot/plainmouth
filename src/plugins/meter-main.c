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

struct meter {
	struct mainwin mainwin;
	bool percent;
	int total;
	int value;
};

static void show_percent(PANEL *panel)
{
	const struct meter *meter = panel_userptr(panel);

	if (meter->percent) {
		mvwprintw(widget_win(&meter->mainwin), 0, (widget_cols(&meter->mainwin) / 2) - 1,
			  "%3d%%", simple_round(((float) meter->value * 100) / (float) meter->total));
		wnoutrefresh(widget_win(&meter->mainwin));
	}
}

static PANEL *p_meter_create(struct request *req)
{
	PANEL *panel;
	struct meter *meter;

	meter = calloc(1, sizeof(*meter));
	if (!meter) {
		warn("calloc meter");
		return NULL;
	}

	meter->percent = req_get_bool(req, "percent", false);
	meter->total = req_get_int(req, "total", 0);

	if (!mainwin_new(req, &meter->mainwin, 1, COLS - 4)) {
		free(meter);
		return NULL;
	}

	if ((panel = mainwin_panel_new(&meter->mainwin, meter)) != NULL) {
		show_percent(panel);
		return panel;
	}

	free(meter);
	return NULL;
}

static enum p_retcode p_meter_delete(PANEL *panel)
{
	struct meter *meter = (struct meter *) panel_userptr(panel);

	mainwin_free(&meter->mainwin);
	mainwin_panel_free(panel);

	return P_RET_OK;
}

static enum p_retcode p_meter_update(struct request *req, PANEL *panel)
{
	struct meter *meter = (struct meter *) panel_userptr(panel);

	int value = req_get_int(req, "value", 0);
	int width = widget_cols(&meter->mainwin);

	if (value < 0)
		value = 0;

	if (value > meter->total)
		value = meter->total;

	meter->value = value;

	int num = simple_round(((float) meter->value / (float) meter->total) * (float) width);

	wmove(widget_win(&meter->mainwin), 0, 0);
	wclrtoeol(widget_win(&meter->mainwin));
	wattron(widget_win(&meter->mainwin), A_REVERSE);

	for (int i = 0; i <= num; i++)
		mvwaddch(widget_win(&meter->mainwin), 0, i, ' ');

	wattroff(widget_win(&meter->mainwin), A_REVERSE);
	show_percent(panel);
	wnoutrefresh(widget_win(&meter->mainwin));

	return P_RET_OK;
}

static bool p_meter_finished(PANEL *panel)
{
	const struct meter *meter = panel_userptr(panel);

	return meter->value == meter->total;
}

struct plugin plugin = {
	.name              = "meter",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_meter_create,
	.p_delete_instance = p_meter_delete,
	.p_update_instance = p_meter_update,
	.p_finished        = p_meter_finished,
	.p_input           = NULL,
	.p_result          = NULL,
	.p_get_cursor      = NULL,
};
