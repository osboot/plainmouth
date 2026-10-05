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

static bool form_positioned_fields(struct request *req, struct widget *scroll)
{
	struct widget *canvas = make_positioned();

	if (!canvas)
		return false;

	widget_add(scroll, canvas);

	struct ipc_pair *pairs = req_data(req);
	int input_id = 1;

	for (size_t i = 0; i < pairs->num_kv; i++) {
		if (!streq(pairs->kv[i].key, "field")) {
			const char *key = pairs->kv[i].key;

			if (streq(key, "hbox") || streq(key, "label") || streq(key, "input") ||
			    streq(key, "password"))
				return req_error(req, "positioned items require field=start/end");

			continue;
		}

		if (!streq(pairs->kv[i].val, "start"))
			return req_error(req, "expected field=start");

		size_t begin = ++i;

		while (i < pairs->num_kv && !streq(pairs->kv[i].key, "field"))
			i++;

		if (i == pairs->num_kv || !streq(pairs->kv[i].val, "end"))
			return req_error(req, "expected field=end");

		for (size_t j = begin; j < i; j++) {
			const char *key = pairs->kv[j].key;

			if (!streq(key, "x") && !streq(key, "y") && !streq(key, "width") &&
			    !streq(key, "label") && !streq(key, "input") && !streq(key, "password") &&
			    !streq(key, "max-length") && !streq(key, "readonly"))
				return req_error(req, "unknown field parameter: %s", key);

			for (size_t k = begin; k < j; k++)
				if (streq(key, pairs->kv[k].key))
					return req_error(req, "duplicate field parameter: %s", key);
		}

		/* Borrow the group's pairs for the existing typed request readers. */
		struct ipc_message message = *req->r_msg;
		message.data.kv = pairs->kv + begin;
		message.data.num_kv = i - begin;
		struct request field = *req;
		field.r_msg = &message;
		int x, y, width;
		bool readonly;

		if (!req_read_int(&field, "x", &x) || !req_read_int(&field, "y", &y) ||
		    !req_read_int(&field, "width", &width) ||
		    !req_read_bool(&field, "readonly", false, &readonly))
			return false;

		if (x < 0 || y < 0 || width < 1 || width > 4096 || x > 4096 - width || y >= 4096)
			return req_error(req, "field geometry exceeds 4096x4096");

		const char *key = NULL;
		const char *kinds[] = { "label", "input", "password" };

		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
			if (!req_get_val(&field, kinds[k]))
				continue;

			if (key)
				return req_error(req, "ambiguous field type");

			key = kinds[k];
		}

		if (!key)
			return req_error(req, "field requires label, input or password");

		wchar_t *value __free(ptr) = req_get_wchars(&field, key);

		if (!value)
			return req_error(req, "unable to decode field value");

		struct widget *child;

		if (streq(key, "label"))
			child = make_label(value);

		else if (streq(key, "password"))
			child = make_input_password(value, NULL);
		else
			child = make_input(value, NULL);

		if (!child)
			return false;

		if (!positioned_add(canvas, child, x, y, width, 1)) {
			widget_free(child);
			return false;
		}

		if (!streq(key, "label")) {
			int id = input_id++;
			if (readonly) {
				bool finished = true;

				widget_set(child, PROP_INPUT_STATE, &finished);
				child->attrs &= ~(ATTR_CAN_FOCUS | ATTR_CAN_CURSOR);
			} else {
				int limit = width;
				bool finish = false;

				if (req_get_val(&field, "max-length") &&
				    !req_read_int(&field, "max-length", &limit))
					return false;

				if (!widget_set(child, PROP_INPUT_MAX_LENGTH, &limit) ||
				    !widget_set(child, PROP_INPUT_FINISH_ON_ENTER, &finish))
					return req_error(req, "invalid input max-length");

				child->w_id = id;
			}
		}

		widget_measure_tree(canvas);

		/* Bound pad allocation as well as each individual coordinate. */
		if ((size_t) canvas->min_w * (size_t) canvas->min_h > 1024 * 1024)
			return req_error(req, "form canvas exceeds 1048576 cells");
	}

	return true;
}

static struct widget *p_form_create(struct request *req)
{
	struct ipc_pair *p = req_data(req);
	struct request *original = req;
	struct ipc_kv *global_pairs __free(ptr) = calloc(p->num_kv, sizeof(*global_pairs));

	if (!global_pairs)
		return NULL;

	struct ipc_message message = *req->r_msg;
	message.data.kv = global_pairs;
	message.data.num_kv = 0;
	bool in_field = false;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "field")) {
			in_field = !streq(p->kv[i].val, "end");
			continue;
		}

		if (!in_field)
			global_pairs[message.data.num_kv++] = p->kv[i];
	}

	/* Field coordinates must not be mistaken for window coordinates. */
	struct request window_request = *req;
	window_request.r_msg = &message;
	req = &window_request;

	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int height  = req_get_int(req, "height", -1);
	int width   = req_get_int(req, "width",  -1);

	if (height < 0 || width < 0) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR='width' and 'height' parameters must be specified",
				req_id(req));
		return NULL;
	}

	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	if (!root)
		return NULL;

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

	struct widget *scroll = make_scroll_vbox();

	if (!scroll)
		goto fail;

	widget_add(parent, scroll);

	const char *layout = req_get_val(req, "layout");

	if (layout && !streq(layout, "positioned") && !streq(layout, "hbox")) {
		req_error(req, "invalid value: layout");
		goto fail;
	}

	bool positioned = layout && streq(layout, "positioned");

	if (positioned && !form_positioned_fields(original, scroll))
		goto fail;

	struct widget *current = NULL;
	int input_id = 1;
	bool finish_on_enter = false;

	for (size_t i = 0; !positioned && i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "hbox")) {
			if (streq(p->kv[i].val, "start")) {
				struct widget *hbox = make_hbox();
				if (!hbox) {
					warnx("unable to create hbox");
					goto fail;
				}
				widget_add(scroll, hbox);

				hbox->flex_h = 0;
				current = hbox;

			} else if (streq(p->kv[i].val, "end")) {
				current = scroll;
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
			if (!widget_set(input, PROP_INPUT_FINISH_ON_ENTER, &finish_on_enter))
				goto fail;

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
			if (!widget_set(input, PROP_INPUT_FINISH_ON_ENTER, &finish_on_enter))
				goto fail;

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

static enum p_retcode p_form_set_value(struct request *req, struct widget *root)
{
	const char *button = req_get_val(req, "button");
	const char *input = req_get_val(req, "input");

	if (button && input) {
		req_error(req, "ambiguous target: button and input");
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

	if (input) {
		int id;
		if (!req_read_int(req, "input", &id))
			return P_RET_ERR;
		if (!req_get_val(req, "value")) {
			req_error(req, "field is missing: value");
			return P_RET_ERR;
		}
		wchar_t *value __free(ptr) = req_get_wchars(req, "value");
		if (!value) {
			req_error(req, "unable to decode value: value");
			return P_RET_ERR;
		}
		struct widget *w = find_widget_by_type_and_id(root, WIDGET_INPUT, id);
		if (!w) {
			req_error(req, "widget not found: input=%d", id);
			return P_RET_ERR;
		}
		if (!widget_set(w, PROP_INPUT_VALUE, value)) {
			req_error(req, "unable to set value: value");
			return P_RET_ERR;
		}
		for (struct widget *parent = w->parent; parent; parent = parent->parent) {
			if (parent->ops && parent->ops->ensure_visible)
				parent->ops->ensure_visible(parent, w);
		}
		return P_RET_OK;
	}

	req_error(req, "field is missing: input or button");
	return P_RET_ERR;
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

PLUGIN_EXPORT
struct plugin plugin = {
	.name                 = "form",
	.desc                 = "The form dialog displays a form consisting of labels and fields.",
	.p_plugin_init        = NULL,
	.p_plugin_free        = NULL,
	.p_create_instance    = p_form_create,
	.p_delete_instance    = NULL,
	.p_update_instance    = NULL,
	.p_set_value_instance = p_form_set_value,
	.p_finished           = p_form_finished,
	.p_result             = p_form_result,
};
