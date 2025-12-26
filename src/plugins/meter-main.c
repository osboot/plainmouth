// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

#define METER_ID 1

static struct widget *p_meter_create(struct request *req)
{
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height  = req_get_int(req, "height", -1);
	int width   = req_get_int(req, "width",  -1);

	int total = req_get_int(req, "total", 0);

	if (height < 0 || width < 0) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR='width' and 'height' parameters must be specified",
				req_id(req));
		return NULL;
	}

	struct widget *root = make_window();
	if (!root)
		return NULL;

	struct widget *parent = root;

	if (req_get_bool(req, "border", false)) {
		struct widget *border = make_border_hbox(parent);
		parent = border;
	}

	wchar_t *label_text __free(ptr) = req_get_wchars(req, "label");
	if (label_text) {
		struct widget *label = make_label(label_text);
		widget_add(parent, label);
	}

	struct widget *meter = make_meter(total);

	meter->w_id = METER_ID;

	widget_add(parent, meter);

	widget_measure_tree(root);

	position_center(width, height, &begin_y, &begin_x);

	widget_layout_tree(root, begin_x, begin_y, width, height);
	widget_create_tree(root);
	widget_render_tree(root);

	return root;
}

static enum p_retcode p_meter_update(struct request *req, struct widget *root)
{
	struct widget *meter = find_widget_by_id(root, METER_ID);
	if (!meter)
		return P_RET_ERR;

	int value = req_get_int(req, "value", 0);

	widget_set(meter, PROP_METER_VALUE, &value);

	return P_RET_OK;
}

static bool p_meter_finished(struct widget *root)
{
	struct widget *meter = find_widget_by_id(root, METER_ID);

	if (meter) {
		int value = 0;
		int total = 0;

		widget_get(meter, PROP_METER_VALUE, &value);
		widget_get(meter, PROP_METER_TOTAL, &total);

		return (total > 0 && value == total);
	}
	return false;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name              = "meter",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_meter_create,
	.p_delete_instance = NULL,
	.p_update_instance = p_meter_update,
	.p_finished        = p_meter_finished,
	.p_result          = NULL,
};
