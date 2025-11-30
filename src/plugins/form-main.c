// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>

#include <curses.h>
#include <panel.h>

#include "helpers.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

static PANEL *p_form_create(struct request *req)
{
	PANEL *panel = NULL;

	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	struct widget *root = make_window();

	/* set root absolute position (0,0) inside screen) */
	root->lx = 1;
	root->ly = 1;
	root->w = cols - 2;
	root->h = rows - 2;

	struct widget *label = make_label("TEXT MESSAGE!");
	widget_add(root, label);

	struct widget *vbox = make_vbox();
	widget_add(root, vbox);

	struct widget *btn1 = make_button("OK");
	struct widget *btn2 = make_button("Cancel");
	struct widget *btn3 = make_button("Extra");

	/* give middle button flex to show stretching */
	btn2->flex = 1;

	widget_add(vbox, btn1);
	widget_add(vbox, btn2);
	widget_add(vbox, btn3);

	/* pipeline */
	/* measure */
	widget_measure(root);
	warnx("XXX root win y=%d, x=%d", root->ly, root->lx);

	/* layout root: here root is full screen */
	widget_layout(root, -1, -1, cols - 2, rows -2);

	/* create windows */
	widget_recreate_windows(root);

	/* initial render */
	widget_render(root);

	assert(root->win != NULL);

	panel = new_panel(root->win);
	//set_panel_userptr(panel, data);

	return panel;
}

static enum p_retcode p_form_delete(PANEL *panel)
{
	return P_RET_OK;
}

static enum p_retcode p_form_result(struct request *req, PANEL *panel)
{
	return P_RET_OK;
}

struct plugin plugin = {
	.name              = "form",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_form_create,
	.p_delete_instance = p_form_delete,
	.p_update_instance = NULL,
	.p_input           = NULL,
	.p_finished        = NULL,
	.p_result          = p_form_result,
	.p_get_cursor      = NULL,
};
