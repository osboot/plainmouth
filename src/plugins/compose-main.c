// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "macros.h"
#include "plugin_helpers.h"
#include "widget.h"

#define COMPOSE_MAX_NODES 256
#define COMPOSE_MAX_DEPTH 32
#define COMPOSE_MAX_SIZE 4096

enum compose_type {
	COMPOSE_VBOX,
	COMPOSE_HBOX,
	COMPOSE_LABEL,
	COMPOSE_INPUT,
	COMPOSE_PASSWORD,
	COMPOSE_CHECKBOX,
	COMPOSE_SELECT,
	COMPOSE_BUTTON,
	COMPOSE_COUNT,
};

static const struct {
	const char *name;
	const char *properties[5];
} types[COMPOSE_COUNT] = {
	[COMPOSE_VBOX]     = { "vbox",     { NULL }                               },
	[COMPOSE_HBOX]     = { "hbox",     { NULL }                               },
	[COMPOSE_LABEL]    = { "label",    { "text", NULL }                       },
	[COMPOSE_INPUT]    = { "input",    { "value", "max-length", NULL }        },
	[COMPOSE_PASSWORD] = { "password", { "value", "max-length", NULL }        },
	[COMPOSE_CHECKBOX] = { "checkbox", { "checked", NULL }                    },
	[COMPOSE_SELECT]   = { "select",   { "option", "visible", "value", NULL } },
	[COMPOSE_BUTTON]   = { "button",   { "text", NULL }                       },
};

static bool property_allowed(enum compose_type type, const char *key)
{
	if (streq(key, "flex-w") || streq(key, "flex-h"))
		return true;

	for (size_t i = 0; types[type].properties[i]; i++) {
		if (streq(key, types[type].properties[i]))
			return true;
	}

	return false;
}

static bool read_number(struct request *req, const char *key, int def,
			int min, int max, int *value)
{
	*value = def;

	if (req_get_val(req, key) && !req_read_int(req, key, value))
		return false;

	if (*value < min || *value > max)
		return req_error(req, "invalid value: %s", key);

	return true;
}

static struct widget *create_node(struct request *req, enum compose_type type)
{
	wchar_t *text __free(ptr) = NULL;
	const char *key = NULL;

	switch (type) {
		case COMPOSE_LABEL:
		case COMPOSE_BUTTON:
			key = "text";
			break;
		case COMPOSE_INPUT:
		case COMPOSE_PASSWORD:
			key = "value";
			break;
		default:
			break;
	}

	if (key) {
		text = req_get_wchars(req, key);

		if (!text || wcslen(text) > COMPOSE_MAX_SIZE) {
			req_error(req, "missing, invalid or oversized node text: %s", key);
			return NULL;
		}
	}

	struct widget *w = NULL;

	switch (type) {
		case COMPOSE_VBOX:
			return make_vbox();
		case COMPOSE_HBOX:
			return make_hbox();
		case COMPOSE_LABEL:
			return make_label(text);
		case COMPOSE_BUTTON:
			return make_button(text);
		case COMPOSE_INPUT:
		case COMPOSE_PASSWORD: {
			int limit;
			bool finish = false;

			if (!read_number(req, "max-length", 65536, 0, 65536, &limit))
				return NULL;

			if (type == COMPOSE_PASSWORD)
				w = make_input_password(text, NULL);
			else
				w = make_input(text, NULL);

			if (w && widget_set(w, PROP_INPUT_MAX_LENGTH, &limit) &&
			    widget_set(w, PROP_INPUT_FINISH_ON_ENTER, &finish))
				return w;

			break;
		}
		case COMPOSE_CHECKBOX: {
			bool checked;

			if (!req_read_bool(req, "checked", false, &checked))
				return NULL;

			return make_checkbox(checked, true);
		}
		case COMPOSE_SELECT: {
			int visible, selected;

			if (!read_number(req, "visible", 3, 1, COMPOSE_MAX_NODES, &visible) ||
			    !read_number(req, "value", 1, 1, COMPOSE_MAX_NODES, &selected))
				return NULL;

			w = make_menu(visible);

			if (!w)
				return NULL;

			struct ipc_pair *pairs = req_data(req);
			int count = 0;

			for (size_t i = 0; i < pairs->num_kv; i++) {
				if (!streq(pairs->kv[i].key, "option"))
					continue;

				wchar_t *option __free(ptr) = req_get_kv_wchars(pairs->kv + i);

				if (!option || wcslen(option) > COMPOSE_MAX_SIZE ||
				    ++count > COMPOSE_MAX_NODES)
					goto fail;

				struct widget *child = make_menu_option(option);

				if (!child)
					goto fail;

				widget_add(w, child);
			}

			int index = selected - 1;

			if (count && widget_set(w, PROP_SELECT_CURSOR, &index))
				return w;

			break;
		}
		default:
			return NULL;
	}

fail:
	widget_free(w);
	req_error(req, "unable to create node: %s", types[type].name);
	return NULL;
}

static struct widget *compose_create(struct request *req)
{
	struct ipc_pair *pairs = req_data(req);
	size_t first = 0;

	while (first < pairs->num_kv && !streq(pairs->kv[first].key, "node"))
		first++;

	for (size_t i = 0; i < first; i++) {
		const char *key = pairs->kv[i].key;

		if (!streq(key, "action") && !streq(key, "plugin") && !streq(key, "id") &&
		    !streq(key, "style") && !streq(key, "width") && !streq(key, "height") &&
		    !streq(key, "x") && !streq(key, "y") && !streq(key, "border")) {
			req_error(req, "unknown compose parameter: %s", key);
			return NULL;
		}

		for (size_t j = 0; j < i; j++) {
			if (streq(key, pairs->kv[j].key)) {
				req_error(req, "duplicate compose parameter: %s", key);
				return NULL;
			}
		}
	}

	/* Window readers must not see node-local properties. */
	struct ipc_message global_message = *req->r_msg;
	global_message.data.num_kv = first;
	struct request global = *req;
	global.r_msg = &global_message;
	int width, height, x, y;
	bool border;

	if (!read_number(&global, "width", 0, 1, COMPOSE_MAX_SIZE, &width) ||
	    !read_number(&global, "height", 0, 1, COMPOSE_MAX_SIZE, &height) ||
	    !read_number(&global, "x", -1, -1, COMPOSE_MAX_SIZE, &x) ||
	    !read_number(&global, "y", -1, -1, COMPOSE_MAX_SIZE, &y) ||
	    !req_read_bool(&global, "border", false, &border))
		return NULL;

	if ((size_t) width * (size_t) height > 1024 * 1024 || first == pairs->num_kv) {
		req_error(req, "compose requires nodes and at most 1048576 window cells");
		return NULL;
	}

	struct widget *content;
	struct widget *root = plugin_create_window(&global, PLUGIN_WINDOW_VERTICAL, &content);

	if (!root)
		return NULL;

	struct widget *stack[COMPOSE_MAX_DEPTH];
	int depth = 0;
	int count = 0;
	bool button = false;

	for (size_t i = first; i < pairs->num_kv;) {
		if (!streq(pairs->kv[i].key, "node")) {
			req_error(req, "node properties must precede children");
			goto fail;
		}

		const char *name = pairs->kv[i++].val;

		if (streq(name, "end")) {
			if (!depth) {
				req_error(req, "unexpected node=end");
				goto fail;
			}

			depth--;
			continue;
		}

		if (count == COMPOSE_MAX_NODES || depth == COMPOSE_MAX_DEPTH) {
			req_error(req, "compose allows at most 256 nodes and 32 levels");
			goto fail;
		}

		size_t begin = i;

		while (i < pairs->num_kv && !streq(pairs->kv[i].key, "node"))
			i++;

		struct ipc_message message = *req->r_msg;
		message.data.kv = pairs->kv + begin;
		message.data.num_kv = i - begin;
		struct request node = *req;
		node.r_msg = &message;
		enum compose_type type;

		for (type = 0; type < COMPOSE_COUNT; type++) {
			if (name && streq(name, types[type].name))
				break;
		}

		if (type == COMPOSE_COUNT) {
			req_error(req, "unknown node type");
			goto fail;
		}

		for (size_t j = begin; j < i; j++) {
			const char *key = pairs->kv[j].key;

			if (!property_allowed(type, key)) {
				req_error(req, "unknown node parameter: %s", key);
				goto fail;
			}

			if (streq(key, "option"))
				continue;

			for (size_t k = begin; k < j; k++) {
				if (streq(key, pairs->kv[k].key)) {
					req_error(req, "duplicate node parameter: %s", key);
					goto fail;
				}
			}
		}

		int flex_w, flex_h;

		if (!read_number(&node, "flex-w", 0, 0, 256, &flex_w) ||
		    !read_number(&node, "flex-h", count ? 0 : 1, 0, 256, &flex_h))
			goto fail;

		if ((!depth && count) ||
		    (!count && type != COMPOSE_VBOX && type != COMPOSE_HBOX) ||
		    (depth && stack[depth - 1]->type != WIDGET_VBOX &&
		     stack[depth - 1]->type != WIDGET_HBOX)) {
			req_error(req, "compose requires one container root; leaves cannot have children");
			goto fail;
		}

		struct widget *w = create_node(&node, type);

		if (!w)
			goto fail;

		w->w_id = ++count;
		w->flex_w = flex_w;
		w->flex_h = flex_h;
		struct widget *parent = content;

		if (depth)
			parent = stack[depth - 1];

		widget_add(parent, w);
		stack[depth++] = w;

		if (type == COMPOSE_BUTTON)
			button = true;
	}

	if (depth) {
		req_error(req, "missing node=end");
		goto fail;
	}

	if (!button) {
		req_error(req, "compose requires a button");
		goto fail;
	}

	widget_measure_tree(root);

	if (root->min_w > width || root->min_h > height) {
		req_error(req, "compose content exceeds window dimensions");
		goto fail;
	}

	position_center(width, height, &y, &x);
	widget_layout_tree(root, x, y, width, height);
	widget_render_tree(root);
	return root;

fail:
	widget_free(root);
	return NULL;
}

static enum p_retcode compose_set_value(struct request *req, struct widget *root)
{
	int id;

	if (!req_read_int(req, "node", &id))
		return P_RET_ERR;

	struct widget *w = find_widget_by_id(root, id);

	if (id <= 0 || !w) {
		req_error(req, "node not found: node=%d", id);
		return P_RET_ERR;
	}

	const char *field = "value";

	if (w->type == WIDGET_CHECKBOX)
		field = "checked";
	else if (w->type == WIDGET_BUTTON)
		field = "clicked";

	struct ipc_pair *pairs = req_data(req);

	for (size_t i = 0; i < pairs->num_kv; i++) {
		const char *key = pairs->kv[i].key;

		if (!streq(key, "action") && !streq(key, "id") &&
		    !streq(key, "node") && !streq(key, field)) {
			req_error(req, "unknown set-value parameter: %s", key);
			return P_RET_ERR;
		}

		for (size_t j = 0; j < i; j++) {
			if (streq(key, pairs->kv[j].key)) {
				req_error(req, "duplicate set-value parameter: %s", key);
				return P_RET_ERR;
			}
		}
	}

	if (w->type != WIDGET_BUTTON && !req_get_val(req, field)) {
		req_error(req, "field is missing: %s", field);
		return P_RET_ERR;
	}

	bool ok = false;

	switch (w->type) {
		case WIDGET_INPUT: {
			wchar_t *value __free(ptr) = req_get_wchars(req, "value");

			if (!value)
				break;

			ok = widget_set(w, PROP_INPUT_VALUE, value);
			break;
		}
		case WIDGET_CHECKBOX: {
			bool checked;

			if (!req_read_bool(req, "checked", false, &checked))
				return P_RET_ERR;

			ok = widget_set(w, PROP_CHECKBOX_STATE, &checked);
			break;
		}
		case WIDGET_SELECT: {
			int option;

			if (!req_read_int(req, "value", &option))
				return P_RET_ERR;

			if (option > 0) {
				int index = option - 1;

				ok = widget_set(w, PROP_SELECT_CURSOR, &index);
			}

			break;
		}
		case WIDGET_BUTTON: {
			bool clicked;

			if (!req_read_bool(req, "clicked", true, &clicked))
				return P_RET_ERR;

			ok = widget_set(w, PROP_BUTTON_STATE, &clicked);
			break;
		}
		default:
			break;
	}

	if (!ok) {
		req_error(req, "unable to set node value: node=%d", id);
		return P_RET_ERR;
	}

	return P_RET_OK;
}

static bool collect_result(struct widget *w, void *data)
{
	struct request *req = data;
	int value = 0;
	bool checked = false;

	if (w->w_id <= 0)
		return true;

	switch (w->type) {
		case WIDGET_INPUT: {
			wchar_t *text = NULL;

			return widget_get(w, PROP_INPUT_VALUE, &text) &&
			       ipc_send_string(req_fd(req), "RESPDATA %s INPUT_%d=%ls", req_id(req), w->w_id, text) > 0;
		}
		case WIDGET_CHECKBOX:
			return widget_get(w, PROP_CHECKBOX_STATE, &checked) &&
			       ipc_send_string(req_fd(req), "RESPDATA %s CHECKBOX_%d=%d", req_id(req), w->w_id, checked) > 0;
		case WIDGET_SELECT:
			return widget_get(w, PROP_SELECT_CURSOR, &value) &&
			       ipc_send_string(req_fd(req), "RESPDATA %s SELECT_%d=%d", req_id(req), w->w_id, value + 1) > 0;
		default:
			return plugin_button_result(req, w);
	}
}

static enum p_retcode compose_result(struct request *req, struct widget *root)
{
	return walk_widget_tree(root, collect_result, req) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name                 = "compose",
	.desc                 = "Construct a dialog from a declarative widget tree.",
	.p_create_instance    = compose_create,
	.p_set_value_instance = compose_set_value,
	.p_finished           = plugin_buttons_finished,
	.p_result             = compose_result,
};
