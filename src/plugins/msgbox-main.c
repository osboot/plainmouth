// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"
#include "plugin_helpers.h"

static struct widget *p_msgbox_create(struct request *req)
{
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height = req_get_int(req, "height", -1);
	int width = req_get_int(req, "width", -1);

	if (height < 0 || width < 0) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR='width' and 'height' parameters must be specified",
				req_id(req));
		return NULL;
	}

	struct widget *root = make_window();
	if (!root)
		return NULL;
	root->style_owner = req->r_style_owner;

	struct widget *parent = root;

	if (req_get_bool(req, "border", false)) {
		struct widget *border = make_border_vbox(parent);
		parent = border;
	}

	wchar_t *text __free(ptr) = req_get_wchars(req, "text");
	if (text) {
		struct widget *txt = make_textview(text);
		widget_add(parent, txt);
		txt->flex_h = 1;
	}

	struct widget *hbox = make_hbox();
	if (!hbox)
		goto fail;
	widget_add(parent, hbox);
	hbox->flex_h = 0;
	if (!plugin_add_buttons(req, hbox))
		goto fail;

	widget_measure_tree(root);

	position_center(width, height, &begin_y, &begin_x);

	widget_layout_tree(root, begin_x, begin_y, width, height);
	widget_render_tree(root);

	return root;
fail:
	widget_free(root);
	return NULL;
}

static bool collect_results(struct widget *w, void *data)
{
	return plugin_button_result(data, w);
}

static enum p_retcode p_msgbox_result(struct request *req, struct widget *root)
{
	return walk_widget_tree(root, collect_results, req) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "msgbox",
	.desc = "The plugin displays a message with one or more buttons at the bottom.",
	.p_plugin_init = NULL,
	.p_plugin_free = NULL,
	.p_create_instance = p_msgbox_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_set_value_instance = plugin_set_button,
	.p_finished = plugin_buttons_finished,
	.p_result = p_msgbox_result,
};
