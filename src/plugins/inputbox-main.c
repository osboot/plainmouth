// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <unistd.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

#define INPUT_ID 1

static struct widget *p_inputbox_create(struct request *req)
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

	wchar_t *top_text __free(ptr) = NULL;
	wchar_t *left_text __free(ptr) = NULL;
	wchar_t *placeholder __free(ptr) = NULL;
	wchar_t *value __free(ptr) = NULL;
	wchar_t *tooltip_text __free(ptr) = NULL;

	struct widget *root = make_window();
	if (!root)
		return NULL;
	root->style_owner = req->r_style_owner;

	struct widget *parent = root;

	if (req_get_bool(req, "border", false)) {
		parent = make_border_vbox(parent);
		if (!parent)
			goto fail;
	}

	top_text = req_get_wchars(req, "text");

	if (req_get_val(req, "text") && !top_text)
		goto fail;

	if (top_text) {
		struct widget *txt = make_textview(top_text);
		if (!txt)
			goto fail;
		txt->flex_h = 1;
		widget_add(parent, txt);
	}

	struct widget *hbox = make_hbox();

	if (!hbox)
		goto fail;

	hbox->flex_h = 0;
	widget_add(parent, hbox);

	left_text = req_get_wchars(req, "label");

	if (req_get_val(req, "label") && !left_text)
		goto fail;

	if (left_text) {
		struct widget *label = make_label(left_text);
		if (!label) {
			warnx("unable to create label");
			goto fail;
		}

		widget_add(hbox, label);
	}

	placeholder = req_get_wchars(req, "placeholder");
	value = req_get_wchars(req, "value");

	if ((req_get_val(req, "value") && !value) ||
	    (req_get_val(req, "placeholder") && !placeholder))
		goto fail;

	struct widget *input = make_input(value, placeholder);

	if (!input) {
		warnx("unable to create input");
		goto fail;
	}
	input->w_id = INPUT_ID;

	widget_add(hbox, input);

	tooltip_text = req_get_wchars(req, "tooltip");

	if (req_get_val(req, "tooltip") && !tooltip_text)
		goto fail;

	if (tooltip_text) {
		struct widget *tooltip = make_tooltip(tooltip_text);
		if (!tooltip) {
			warnx("unable to create tooltip");
			goto fail;
		}
		widget_add(hbox, tooltip);
	}

	struct ipc_pair *p = req_data(req);
	struct widget *buttons = NULL;
	int button_id = 1;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (!streq(p->kv[i].key, "button"))
			continue;
		if (!buttons) {
			buttons = make_hbox();
			if (!buttons)
				goto fail;
			buttons->flex_h = 0;
			widget_add(parent, buttons);
		}
		wchar_t *label __free(ptr) = req_get_kv_wchars(p->kv + i);
		if (!label)
			goto fail;
		struct widget *button = make_button(label);
		if (!button)
			goto fail;
		button->w_id = button_id++;
		widget_add(buttons, button);
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

	if (w->w_id > 0 && w->type == WIDGET_INPUT) {
		wchar_t *text = NULL;
		widget_get(w, PROP_INPUT_VALUE, &text);

		ipc_send_string(req_fd(req), "RESPDATA %s INPUT_%d=%ls",
				req_id(req), w->w_id, text);
	}

	if (w->w_id > 0 && w->type == WIDGET_BUTTON) {
		bool clicked = false;
		widget_get(w, PROP_BUTTON_STATE, &clicked);
		ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d",
				req_id(req), w->w_id, clicked);
	}

	return true;
}

static enum p_retcode p_inputbox_result(struct request *req, struct widget *root)
{
	walk_widget_tree(root, collect_results, req);
	return P_RET_OK;
}

static enum p_retcode p_inputbox_set_value(struct request *req, struct widget *root)
{
	const char *button = req_get_val(req, "button");

	if (button) {
		if (req_get_val(req, "value") || req_get_val(req, "finished")) {
			req_error(req, "ambiguous target: button and input");
			return P_RET_ERR;
		}

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

	bool has_value = req_get_val(req, "value") != NULL;
	bool has_finished = req_get_val(req, "finished") != NULL;
	bool finished;

	if (!has_value && !has_finished) {
		req_error(req, "field is missing: value, finished or button");
		return P_RET_ERR;
	}

	if (!req_read_bool(req, "finished", false, &finished))
		return P_RET_ERR;

	wchar_t *value __free(ptr) = has_value ? req_get_wchars(req, "value") : NULL;

	if (has_value && !value) {
		req_error(req, "unable to decode value: value");
		return P_RET_ERR;
	}

	struct widget *w = find_widget_by_type_and_id(root, WIDGET_INPUT, INPUT_ID);

	if (!w) {
		req_error(req, "widget not found: input=%d", INPUT_ID);
		return P_RET_ERR;
	}

	if (has_value && !widget_set(w, PROP_INPUT_VALUE, value)) {
		req_error(req, "unable to set value: value");
		return P_RET_ERR;
	}

	if (has_finished && !widget_set(w, PROP_INPUT_STATE, &finished)) {
		req_error(req, "unable to set value: finished");
		return P_RET_ERR;
	}

	return P_RET_OK;
}

static bool check_finished(struct widget *w, void *data)
{
	bool finished = false;

	if (w->type == WIDGET_INPUT)
		widget_get(w, PROP_INPUT_STATE, &finished);

	else if (w->type == WIDGET_BUTTON)
		widget_get(w, PROP_BUTTON_STATE, &finished);

	if (finished)
		*(bool *) data = true;

	return !finished;
}

static bool p_inputbox_finished(struct widget *root)
{
	bool finished = false;
	walk_widget_tree(root, check_finished, &finished);
	return finished;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "inputbox",
	.desc = "The plugin displays a single-line text entry dialog.",
	.p_plugin_init = NULL,
	.p_plugin_free = NULL,
	.p_create_instance = p_inputbox_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_set_value_instance = p_inputbox_set_value,
	.p_finished = p_inputbox_finished,
	.p_result = p_inputbox_result,
};
