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
#include "warray.h"
#include "widget.h"
#include "plugin.h"
#include "plugin_helpers.h"

#define SELECT_ID 1

static struct widget *p_checklist_create(struct request *req)
{
	struct ipc_pair *p = req_data(req);

	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height  = req_get_int(req, "height", -1);
	int width   = req_get_int(req, "width",  -1);

	if (height < 0 || width < 0) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR='width' and 'height' parameters must be specified",
				req_id(req));
		return NULL;
	}

	wchar_t *text __free(ptr) = NULL;
	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	if (!root)
		return NULL;

	text = req_get_wchars(req, "text");
	if (text) {
		struct widget *txt = make_textview(text);
		if (!txt) {
			warnx("unable to create textview");
			goto fail;
		}
		widget_add(parent, txt);
		txt->flex_h = 1;
	}

	int maxsel = req_get_int(req, "select", 1);
	int maxvis = req_get_int(req, "visible", 1);

	struct widget *select = make_select(maxsel, maxvis);
	if (!select) {
		warnx("unable to create select");
		goto fail;
	}
	widget_add(parent, select);
	select->w_id = SELECT_ID;

	int initially_selected = 0;
	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "status")) {
			req_error(req, "status must follow option");
			goto fail;
		}
		if (streq(p->kv[i].key, "option")) {
			bool checked = false;
			size_t status_index = i + 1;
			if (status_index < p->num_kv && streq(p->kv[status_index].key, "status")) {
				if (!req_read_kv_bool(req, p->kv + status_index, &checked))
					goto fail;
			}
			if (checked && ++initially_selected > maxsel) {
				req_error(req, "initial selection exceeds select limit");
				goto fail;
			}
			wchar_t *txt = req_get_kv_wchars(p->kv + i);
			struct widget *option = txt ? make_select_option(txt, checked, (maxsel > 1)) : NULL;
			free(txt);
			if (!option)
				goto fail;
			widget_add(select, option);
			if (status_index < p->num_kv && streq(p->kv[status_index].key, "status"))
				i = status_index;
		}
	}

	struct widget *hbox = make_hbox();
	if (!hbox) {
		warnx("unable to create hbox");
		goto fail;
	}
	widget_add(parent, hbox);

	int button_id = 1;
	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "button")) {
			wchar_t *label = req_get_kv_wchars(p->kv + i);

			struct widget *btn = make_button(label);
			free(label);

			if (!btn) {
				warnx("unable to create button");
				goto fail;
			}
			widget_add(hbox, btn);
			btn->w_id = button_id++;
		}
	}

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

	if (w->type == WIDGET_SELECT) {
		int options = 0;
		widget_get(w, PROP_SELECT_OPTIONS_SIZE, &options);

		/*
		 * OPTION_N is the stable 1-based id assigned by create-request
		 * order, not a visual row number.
		 */
		for (int i = 0; i < options; i++) {
			bool selected = false;
			widget_get_index(w, PROP_SELECT_OPTION_VALUE, i, &selected);

			ipc_send_string(req_fd(req), "RESPDATA %s SELECT_%d_OPTION_%d=%d",
					req_id(req), w->w_id, (i + 1), selected);
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

static enum p_retcode p_checklist_set_value(struct request *req, struct widget *root)
{
	const char *button = req_get_val(req, "button");
	const char *option = req_get_val(req, "option");

	if (button && option) {
		req_error(req, "ambiguous target: button and option");
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

	if (option) {
		int id;
		int select_id = SELECT_ID;
		bool selected;
		if (!req_read_int(req, "option", &id) ||
		    (req_get_val(req, "select") && !req_read_int(req, "select", &select_id)) ||
		    !req_read_bool(req, "selected", true, &selected))
			return P_RET_ERR;
		if (id <= 0) {
			req_error(req, "invalid value: option");
			return P_RET_ERR;
		}
		struct widget *w = find_widget_by_type_and_id(root, WIDGET_SELECT, select_id);
		if (!w) {
			req_error(req, "widget not found: select=%d", select_id);
			return P_RET_ERR;
		}
		if (!widget_set_index(w, PROP_SELECT_OPTION_VALUE, id - 1, &selected)) {
			req_error(req, "option not found: option=%d", id);
			return P_RET_ERR;
		}
		return P_RET_OK;
	}

	req_error(req, "field is missing: option or button");
	return P_RET_ERR;
}

static enum p_retcode p_checklist_result(struct request *req, struct widget *root)
{
	walk_widget_tree(root, collect_results, req);
	return P_RET_OK;
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

static bool p_checklist_finished(struct widget *root)
{
	bool is_finished = false;
	walk_widget_tree(root, check_results, &is_finished);
	return is_finished;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name              = "checklistbox",
	.desc              = "A checklist box. There are multiple entries presented in the form of a menu.",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_checklist_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_set_value_instance = p_checklist_set_value,
	.p_finished        = p_checklist_finished,
	.p_result          = p_checklist_result,
};
