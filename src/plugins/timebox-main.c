// SPDX-License-Identifier: GPL-2.0-or-later
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

#define SPIN_HOUR_ID 1
#define SPIN_MIN_ID  2
#define SPIN_SEC_ID  3

static struct widget *p_timebox_create(struct request *req)
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
		widget_add(parent, txt);
		txt->flex_h = 1;
	}

	int h, m, s;

	h = m = s = 0;

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

	hbox1->flex_h = 0;

	struct widget *hbox2 = make_hbox();

	if (!hbox2) {
		warnx("unable to create hbox");
		goto fail;
	}
	widget_add(parent, hbox2);

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
			widget_add(hbox2, btn);
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

struct plugin plugin = {
	.name              = "timebox",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_timebox_create,
	.p_delete_instance = NULL,
	.p_update_instance = NULL,
	.p_input           = NULL,
	.p_finished        = p_timebox_finished,
	.p_result          = p_timebox_result,
};
