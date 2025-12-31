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

static struct widget *p_pass_create(struct request *req)
{
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height  = req_get_int(req, "height", -1);
	int width   = req_get_int(req, "width",  -1);

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
		struct widget *border = make_border_vbox(parent);
		parent = border;
	}

	wchar_t *top_text __free(ptr) = req_get_wchars(req, "text");

	if (top_text) {
		struct widget *txt = make_textview(top_text);
		widget_add(parent, txt);
	}

	struct widget *hbox = make_hbox();
	widget_add(parent, hbox);

	wchar_t *left_text __free(ptr) = req_get_wchars(req, "label");

	if (left_text) {
		struct widget *label = make_label(left_text);
		if (!label) {
			warnx("unable to create label");
			widget_free(root);
			return NULL;
		}

		widget_add(hbox, label);
	}

	wchar_t *placeholder __free(ptr) = req_get_wchars(req, "placeholder");

	struct widget *input = make_input_password(NULL, placeholder);
	if (!input) {
		warnx("unable to create input");
		widget_free(root);
		return NULL;
	}
	input->w_id = INPUT_ID;

	widget_add(hbox, input);

	wchar_t *tooltip_text __free(ptr) = req_get_wchars(req, "tooltip");

	if (tooltip_text) {
		struct widget *tooltip = make_tooltip(tooltip_text);
		if (!tooltip) {
			warnx("unable to create tooltip");
			widget_free(root);
			return NULL;
		}
		widget_add(hbox, tooltip);
	}

	widget_measure_tree(root);

	position_center(width, height, &begin_y, &begin_x);

	widget_layout_tree(root, begin_x, begin_y, width, height);
	widget_render_tree(root);

	return root;
}

static bool collect_results(struct widget *w, void *data)
{
	struct request *req = data;

	if (w->w_id > 0 && w->type == WIDGET_INPUT) {
		wchar_t *text = NULL;
		widget_get(w, PROP_INPUT_VALUE, &text);

		ipc_send_string(req_fd(req), "RESPDATA %s PASSWORD_%d=%ls",
				req_id(req), w->w_id, text);
	}

	return true;
}

static enum p_retcode p_pass_result(struct request *req, struct widget *root)
{
	walk_widget_tree(root, collect_results, req);
	return P_RET_OK;
}

static bool p_pass_finished(struct widget *root)
{
	bool is_finished = false;
	struct widget *input = find_widget_by_id(root, INPUT_ID);

	if (input)
		widget_get(input, PROP_INPUT_STATE, &is_finished);

	return is_finished;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name              = "password",
	.desc              = "The plugin displays a password entry dialog.",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_pass_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_finished        = p_pass_finished,
	.p_result          = p_pass_result,
};
