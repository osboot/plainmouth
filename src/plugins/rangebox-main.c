// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "macros.h"
#include "plugin.h"
#include "plugin_helpers.h"
#include "widget.h"

static struct widget *rangebox_create(struct request *req)
{
	int width, height, min, max, value;
	if (!req_read_int(req, "width", &width) || !req_read_int(req, "height", &height) ||
	    !req_read_int(req, "min", &min) || !req_read_int(req, "max", &max) ||
	    !req_read_int(req, "value", &value))
		return NULL;
	if (width < 3 || height < 4 || min > max || value < min || value > max) {
		req_error(req, "invalid rangebox dimensions or range");
		return NULL;
	}
	wchar_t *text __free(ptr) = req_get_wchars(req, "text");
	if (req_get_val(req, "text") && !text)
		return NULL;
	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	if (!root)
		return NULL;
	if (text) {
		struct widget *view = make_textview(text);
		if (!view)
			goto fail;
		widget_add(parent, view);
	}
	char low[32], high[32];
	int low_width = snprintf(low, sizeof(low), "%d", min);
	int high_width = snprintf(high, sizeof(high), "%d", max);
	struct widget *spin = make_spinbox(min, max, 1, value, MAX(low_width, high_width));
	if (!spin)
		goto fail;
	spin->w_id = 1;
	widget_add(parent, spin);
	struct widget *buttons = make_hbox();
	if (!buttons)
		goto fail;
	buttons->flex_h = 0;
	widget_add(parent, buttons);
	struct ipc_pair *pairs = req_data(req);
	int id = 1;
	for (size_t i = 0; i < pairs->num_kv; i++) {
		if (!streq(pairs->kv[i].key, "button"))
			continue;
		wchar_t *label __free(ptr) = req_get_kv_wchars(pairs->kv + i);
		struct widget *button = label ? make_button(label) : NULL;
		if (!button)
			goto fail;
		button->w_id = id++;
		widget_add(buttons, button);
	}
	int x = req_get_int(req, "x", -1), y = req_get_int(req, "y", -1);
	position_center(width, height, &y, &x);
	widget_measure_tree(root);
	widget_layout_tree(root, x, y, width, height);
	widget_render_tree(root);
	return root;
fail:
	widget_free(root);
	return NULL;
}

static bool rangebox_collect(struct widget *w, void *data)
{
	struct request *req = data;
	if (w->type == WIDGET_SPINBOX) {
		int value;
		widget_get(w, PROP_SPINBOX_VALUE, &value);
		ipc_send_string(req_fd(req), "RESPDATA %s VALUE=%d", req_id(req), value);
	} else if (w->type == WIDGET_BUTTON) {
		bool clicked = false;
		widget_get(w, PROP_BUTTON_STATE, &clicked);
		ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d", req_id(req), w->w_id, clicked);
	}
	return true;
}

static enum p_retcode rangebox_result(struct request *req, struct widget *root)
{
	walk_widget_tree(root, rangebox_collect, req);
	return P_RET_OK;
}

static bool rangebox_check(struct widget *w, void *data)
{
	bool clicked = false;
	if (w->type == WIDGET_BUTTON)
		widget_get(w, PROP_BUTTON_STATE, &clicked);
	if (clicked)
		*(bool *) data = true;
	return !clicked;
}

static bool rangebox_finished(struct widget *root)
{
	bool finished = false;
	walk_widget_tree(root, rangebox_check, &finished);
	return finished;
}

static enum p_retcode rangebox_set_value(struct request *req, struct widget *root)
{
	if (req_get_val(req, "button")) {
		if (req_get_val(req, "value")) {
			req_error(req, "ambiguous target: button and value");
			return P_RET_ERR;
		}
		int id;
		bool clicked;
		if (!req_read_int(req, "button", &id) || !req_read_bool(req, "clicked", true, &clicked))
			return P_RET_ERR;
		struct widget *button = find_widget_by_type_and_id(root, WIDGET_BUTTON, id);
		if (!button) {
			req_error(req, "widget not found: button=%d", id);
			return P_RET_ERR;
		}
		return widget_set(button, PROP_BUTTON_STATE, &clicked) ? P_RET_OK : P_RET_ERR;
	}
	int value;
	if (!req_read_int(req, "value", &value))
		return P_RET_ERR;
	struct widget *spin = find_widget_by_type_and_id(root, WIDGET_SPINBOX, 1);
	return widget_set(spin, PROP_SPINBOX_VALUE, &value) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "rangebox",
	.desc = "Select an integer within a range.",
	.p_create_instance = rangebox_create,
	.p_set_value_instance = rangebox_set_value,
	.p_finished = rangebox_finished,
	.p_result = rangebox_result,
};
