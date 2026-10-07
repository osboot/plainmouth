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

#define SPIN_HOUR_ID 1
#define SPIN_MIN_ID  2
#define SPIN_SEC_ID  3

static struct widget *p_timebox_create(struct request *req)
{
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height  = req_get_int(req, "height", -1);
	int width   = req_get_int(req, "width",  -1);
	int h = 0, m = 0, s = 0;

	if (height < 0 || width < 0) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR='width' and 'height' parameters must be specified",
				req_id(req));
		return NULL;
	}
	if ((req_get_val(req, "hour") && !req_read_int(req, "hour", &h)) ||
	    (req_get_val(req, "minute") && !req_read_int(req, "minute", &m)) ||
	    (req_get_val(req, "second") && !req_read_int(req, "second", &s)))
		return NULL;
	if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
		req_error(req, "invalid timebox value");
		return NULL;
	}

	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	if (!root)
		return NULL;

	wchar_t *text __free(ptr) = req_get_wchars(req, "text");
	if (text) {
		struct widget *txt = plugin_create_textview(text);

		if (!txt)
			goto fail;

		widget_add(parent, txt);
		txt->flex_h = 1;
	}

	struct widget *hbox1 = make_hbox();
	struct widget *hour  = make_spinbox(0, 23, 1, h, 2);
	struct widget *sep1  = make_label(L":");
	struct widget *min   = make_spinbox(0, 59, 1, m, 2);
	struct widget *sep2  = make_label(L":");
	struct widget *sec   = make_spinbox(0, 59, 1, s, 2);

	if (!hbox1 || !hour || !sep1 || !min || !sep2 || !sec) {
		warnx("unable to create timebox widgets");
		goto fail;
	}

	hour->w_id = SPIN_HOUR_ID;
	min->w_id  = SPIN_MIN_ID;
	sec->w_id  = SPIN_SEC_ID;

	widget_add(parent, hbox1);
	widget_add(hbox1, hour);
	widget_add(hbox1, sep1);
	widget_add(hbox1, min);
	widget_add(hbox1, sep2);
	widget_add(hbox1, sec);

	if (!widget_set_node_id(hour, "hour") || !widget_set_node_id(min, "minute") ||
	    !widget_set_node_id(sec, "second"))
		goto fail;

	hbox1->flex_h = 0;

	struct widget *hbox2 = make_hbox();

	if (!hbox2) {
		warnx("unable to create hbox");
		goto fail;
	}
	widget_add(parent, hbox2);

	if (!plugin_add_buttons(req, hbox2))
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
	struct request *req = data;

	if (w->w_id <= 0)
		return true;

	if (w->type == WIDGET_SPINBOX) {
		int value = 0;
		widget_get(w, PROP_SPINBOX_VALUE, &value);

		switch (w->w_id) {
			case SPIN_HOUR_ID:
				ipc_send_string(req_fd(req), "RESPDATA %s SPINBOX_HOURS=%d",
					req_id(req), value);
				break;

			case SPIN_MIN_ID:
				ipc_send_string(req_fd(req), "RESPDATA %s SPINBOX_MINUTES=%d",
					req_id(req), value);
				break;

			case SPIN_SEC_ID:
				ipc_send_string(req_fd(req), "RESPDATA %s SPINBOX_SECONDS=%d",
					req_id(req), value);
				break;
		}
	}

	if (w->type == WIDGET_BUTTON) {
		bool clicked = false;
		widget_get(w, PROP_BUTTON_STATE, &clicked);

		ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d",
				req_id(req), w->w_id, clicked);
	}

	return true;
}

static enum p_retcode p_timebox_result(struct request *req, struct widget *root)
{
	walk_widget_tree(root, collect_results, req);
	return P_RET_OK;
}

static enum p_retcode p_timebox_set_value(struct request *req, struct widget *root)
{
	const char *button = req_get_val(req, "button");
	const char *spinbox = req_get_val(req, "spinbox");

	if (button && spinbox) {
		req_error(req, "ambiguous target: button and spinbox");
		return P_RET_ERR;
	}

	if (button) {
		int id;
		bool clicked;
		if (!req_read_int(req, "button", &id) ||
		    !req_read_bool(req, "clicked", true, &clicked))
			return P_RET_ERR;
		struct widget *w = find_widget_by_type_and_id(root, WIDGET_BUTTON, id);
		if (!w) {
			req_error(req, "widget not found: button=%d", id);
			return P_RET_ERR;
		}
		if (!widget_set(w, PROP_BUTTON_STATE, &clicked)) {
			req_error(req, "unable to set value: clicked");
			return P_RET_ERR;
		}
		return P_RET_OK;
	}

	if (spinbox) {
		int id;
		if (!req_read_int(req, "spinbox", &id))
			return P_RET_ERR;
		if (!req_get_val(req, "value")) {
			req_error(req, "field is missing: value");
			return P_RET_ERR;
		}
		int value;
		if (!req_read_int(req, "value", &value))
			return P_RET_ERR;
		struct widget *w = find_widget_by_type_and_id(root, WIDGET_SPINBOX, id);
		if (!w) {
			req_error(req, "widget not found: spinbox=%d", id);
			return P_RET_ERR;
		}
		if (!widget_set(w, PROP_SPINBOX_VALUE, &value)) {
			req_error(req, "unable to set value: value");
			return P_RET_ERR;
		}
		return P_RET_OK;
	}

	req_error(req, "field is missing: spinbox or button");
	return P_RET_ERR;
}

static bool check_results(struct widget *w, void *data)
{
	bool *is_finished = data;

	if (w->w_id > 0 && w->type == WIDGET_BUTTON) {
		bool clicked = false;
		widget_get(w, PROP_BUTTON_STATE, &clicked);

		if (clicked)
			*is_finished = true;
	}
	return true;
}

static bool p_timebox_finished(struct widget *root)
{
	bool is_finished = false;
	walk_widget_tree(root, check_results, &is_finished);
	return is_finished;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name              = "timebox",
	.desc              = "A dialog is displayed which allows you to select hour, minute and second.",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_timebox_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_set_value_instance = p_timebox_set_value,
	.p_finished        = p_timebox_finished,
	.p_result          = p_timebox_result,
};
