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
	struct widget *vbox = make_vbox();
	widget_add(root, vbox);

	struct widget *btn1 = make_button("OK");
	struct widget *btn2 = make_button("Cancel");
	struct widget *btn3 = make_button("Extra");

	widget_add(vbox, btn1);
	widget_add(vbox, btn2);
	widget_add(vbox, btn3);

	widget_measure(root);
	widget_layout(root, 1, 1, cols - 2, rows - 2);
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
