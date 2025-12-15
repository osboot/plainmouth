// SPDX-License-Identifier: GPL-2.0-or-later
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


static struct widget *p_form_create(struct request *req)
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

	if (req_get_bool(req, "border", true)) {
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

	struct widget *current = NULL;
	int input_id = 1;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "hbox")) {
			if (streq(p->kv[i].val, "start")) {
				struct widget *hbox = make_hbox();
				if (!hbox) {
					warnx("unable to create hbox");
					goto fail;
				}
				widget_add(parent, hbox);

				hbox->flex_h = 0;
				current = hbox;

			} else if (streq(p->kv[i].val, "end")) {
				current = parent;
			}
			continue;
		}
		if (streq(p->kv[i].key, "label")) {
			wchar_t *s = req_get_kv_wchars(p->kv + i);

			struct widget *label = make_label(s);
			free(s);

			if (!label) {
				warnx("unable to create label");
				goto fail;
			}
			widget_add(current, label);
			continue;
		}
		if (streq(p->kv[i].key, "input")) {
			wchar_t *s = req_get_kv_wchars(p->kv + i);

			struct widget *input = make_input(s, NULL);
			free(s);

			if (!input) {
				warnx("unable to create input");
				goto fail;
			}
			widget_add(current, input);

			input->w_id = input_id++;
			continue;
		}
		if (streq(p->kv[i].key, "password")) {
			wchar_t *s = req_get_kv_wchars(p->kv + i);

			struct widget *input = make_input_password(s, NULL);
			free(s);

			if (!input) {
				warnx("unable to create input");
				goto fail;
			}
			widget_add(current, input);

			input->w_id = input_id++;
			continue;
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
	widget_create_tree(root);
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

	if (w->w_id > 0 && w->type == WIDGET_INPUT) {
		wchar_t *text = NULL;
		widget_get(w, PROP_INPUT_VALUE, &text);

		ipc_send_string(req_fd(req), "RESPDATA %s INPUT_%d=%ls",
				req_id(req), w->w_id, text);
	}

	if (w->type == WIDGET_BUTTON) {
		bool clicked = false;
		widget_get(w, PROP_BUTTON_STATE, &clicked);

		ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d",
				req_id(req), w->w_id, clicked);
	}

	return true;
}

static enum p_retcode p_form_result(struct request *req, struct widget *root)
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

static bool p_form_finished(struct widget *root)
{
	bool is_finished = false;
	walk_widget_tree(root, check_results, &is_finished);
	return is_finished;
}

struct plugin plugin = {
	.name              = "form",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_form_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_input           = NULL,
	.p_finished        = p_form_finished,
	.p_result          = p_form_result,
};
