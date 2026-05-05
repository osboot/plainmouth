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

	struct widget *root = make_window();
	if (!root)
		return NULL;

	struct widget *parent = root;

	if (req_get_bool(req, "border", false)) {
		struct widget *border = make_border_vbox(parent);
		parent = border;
	}

	wchar_t *text __free(ptr) = req_get_wchars(req, "text");
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

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "option")) {
			wchar_t *txt = req_get_kv_wchars(p->kv + i);
			struct widget *option = make_select_option(txt, false, (maxsel > 1));
			free(txt);
			widget_add(select, option);
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

struct find_widget_ctx {
	enum widget_type type;
	int id;
	struct widget *found;
};

static bool find_widget_by_type_and_id(struct widget *w, void *data)
{
	struct find_widget_ctx *ctx = data;

	if (w->type == ctx->type && w->w_id == ctx->id) {
		ctx->found = w;
		return false;
	}
	return true;
}

static enum p_retcode p_checklist_set_value(struct request *req, struct widget *root)
{
	const char *button = req_get_val(req, "button");
	const char *option = req_get_val(req, "option");

	if (button) {
		struct find_widget_ctx ctx = {
			.type = WIDGET_BUTTON,
			.id = req_get_int(req, "button", -1),
		};
		bool clicked = req_get_bool(req, "clicked", true);

		walk_widget_tree(root, find_widget_by_type_and_id, &ctx);
		if (!ctx.found || !widget_set(ctx.found, PROP_BUTTON_STATE, &clicked)) {
			ipc_send_string(req_fd(req), "RESPDATA %s ERR=button not found: %s",
					req_id(req), button);
			return P_RET_ERR;
		}
		return P_RET_OK;
	}

	if (option) {
		struct find_widget_ctx ctx = {
			.type = WIDGET_SELECT,
			.id = req_get_int(req, "select", SELECT_ID),
		};
		int option_id = req_get_int(req, "option", 0);
		bool selected = req_get_bool(req, "selected", true);

		walk_widget_tree(root, find_widget_by_type_and_id, &ctx);
		if (!ctx.found || option_id <= 0 ||
		    !widget_set_index(ctx.found, PROP_SELECT_OPTION_VALUE, option_id - 1, &selected)) {
			ipc_send_string(req_fd(req), "RESPDATA %s ERR=option not found: %s",
					req_id(req), option);
			return P_RET_ERR;
		}
		return P_RET_OK;
	}

	ipc_send_string(req_fd(req), "RESPDATA %s ERR=set-value requires button or option",
			req_id(req));
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
	.name              = "checklist",
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
