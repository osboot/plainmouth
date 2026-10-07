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
	COMPOSE_SCROLL,
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
	[COMPOSE_SCROLL]   = { "scroll",   { NULL }                               },
	[COMPOSE_LABEL]    = { "label",    { "text", NULL }                       },
	[COMPOSE_INPUT]    = { "input",    { "value", "max-length", NULL }        },
	[COMPOSE_PASSWORD] = { "password", { "value", "max-length", NULL }        },
	[COMPOSE_CHECKBOX] = { "checkbox", { "checked", NULL }                    },
	[COMPOSE_SELECT]   = { "select",   { "option", "visible", "value", NULL } },
	[COMPOSE_BUTTON]   = { "button",   { "text", "close", NULL }              },
};

static bool property_allowed(enum compose_type type, const char *key)
{
	if (streq(key, "flex-w") || streq(key, "flex-h") ||
	    streq(key, "disabled") || streq(key, "readonly") || streq(key, "node-id"))
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

struct node_lookup {
	const char *name;
	struct widget *found;
};

static bool match_node_id(struct widget *w, void *data)
{
	struct node_lookup *lookup = data;

	if (w->node_id && streq(w->node_id, lookup->name)) {
		lookup->found = w;
		return false;
	}

	return true;
}

static struct widget *find_node_id(struct widget *root, const char *name)
{
	struct node_lookup lookup = { .name = name };
	walk_widget_tree(root, match_node_id, &lookup);
	return lookup.found;
}

static bool valid_node_id(const char *name)
{
	if (!*name || strlen(name) > WIDGET_NODE_ID_MAX)
		return false;

	for (const char *p = name; *p; p++) {
		if (!(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z') &&
		    !(*p >= '0' && *p <= '9') && *p != '_' && *p != '-' && *p != '.')
			return false;
	}

	return true;
}

static struct widget *resolve_node(struct request *req, struct widget *root)
{
	const char *name = req_get_val(req, "node-id");
	const char *number = req_get_val(req, "node");

	if ((!name && !number) || (name && number)) {
		req_error(req, "provide exactly one of node or node-id");
		return NULL;
	}

	struct widget *w = NULL;

	if (name) {
		if (!valid_node_id(name)) {
			req_error(req, "invalid node-id");
			return NULL;
		}

		w = find_node_id(root, name);
	} else {
		int id;

		if (!req_read_int(req, "node", &id))
			return NULL;

		if (id > 0)
			w = find_widget_by_id(root, id);
	}

	if (!w)
		req_error(req, "node not found");

	return w;
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
		case COMPOSE_SCROLL:
			return make_scroll_vbox();
		case COMPOSE_LABEL:
			return make_label(text);
		case COMPOSE_BUTTON: {
			bool close;

			if (!req_read_bool(req, "close", true, &close))
				return NULL;

			w = make_button(text);

			if (w && widget_set(w, PROP_BUTTON_CLOSE, &close))
				return w;

			break;
		}
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

static bool validate_pad_size(struct widget *w, void *data)
{
	if (w->type != WIDGET_PAD_BOX)
		return true;

	int width, height;

	if (!widget_get(w, PROP_SCROLL_CONTENT_W, &width) ||
	    !widget_get(w, PROP_SCROLL_CONTENT_H, &height))
		return false;

	if (width > COMPOSE_MAX_SIZE || height > COMPOSE_MAX_SIZE ||
	    (size_t) width * (size_t) height > 1024 * 1024)
		return req_error(data, "compose scroll content exceeds 4096 per axis or 1048576 cells");

	return true;
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
		bool disabled, readonly;

		if (!read_number(&node, "flex-w", 0, 0, 256, &flex_w) ||
		    !read_number(&node, "flex-h", count ? 0 : 1, 0, 256, &flex_h) ||
		    !req_read_bool(&node, "disabled", false, &disabled) ||
		    !req_read_bool(&node, "readonly", false, &readonly))
			goto fail;

		if ((!depth && count) ||
		    (!count && type != COMPOSE_VBOX && type != COMPOSE_HBOX) ||
		    (depth && stack[depth - 1]->type != WIDGET_VBOX &&
		     stack[depth - 1]->type != WIDGET_HBOX &&
		     stack[depth - 1]->type != WIDGET_SCROLL_VBOX)) {
			req_error(req, "compose requires one container root; leaves cannot have children");
			goto fail;
		}

		const char *node_id = req_get_val(&node, "node-id");

		if (node_id && (!valid_node_id(node_id) || find_node_id(root, node_id))) {
			req_error(req, "invalid or duplicate node-id: %s", node_id);
			goto fail;
		}

		struct widget *w = create_node(&node, type);

		if (!w)
			goto fail;

		if (node_id) {
			w->node_id = strdup(node_id);

			if (!w->node_id) {
				widget_free(w);
				req_error(req, "no memory");
				goto fail;
			}
		}

		w->w_id = ++count;
		w->flex_w = flex_w;
		w->flex_h = flex_h;

		if (disabled)
			w->attrs |= ATTR_DISABLED;

		if (readonly)
			w->attrs |= ATTR_READONLY;

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

	if (!walk_widget_tree(root, validate_pad_size, req))
		goto fail;

	widget_render_tree(root);
	return root;

fail:
	widget_free(root);
	return NULL;
}

static enum p_retcode compose_update(struct request *req, struct widget *root)
{
	struct ipc_pair *pairs = req_data(req);

	for (size_t i = 0; i < pairs->num_kv; i++) {
		const char *key = pairs->kv[i].key;

		if (!streq(key, "action") && !streq(key, "id") && !streq(key, "node") && !streq(key, "node-id") &&
		    !streq(key, "disabled") && !streq(key, "readonly")) {
			req_error(req, "unknown update parameter: %s", key);
			return P_RET_ERR;
		}

		for (size_t j = 0; j < i; j++) {
			if (streq(key, pairs->kv[j].key)) {
				req_error(req, "duplicate update parameter: %s", key);
				return P_RET_ERR;
			}
		}
	}

	bool disabled, readonly;

	if (!req_read_bool(req, "disabled", false, &disabled) ||
	    !req_read_bool(req, "readonly", false, &readonly))
		return P_RET_ERR;

	struct widget *w = resolve_node(req, root);

	if (!w)
		return P_RET_ERR;

	if (!req_get_val(req, "disabled") && !req_get_val(req, "readonly")) {
		req_error(req, "update requires disabled or readonly");
		return P_RET_ERR;
	}

	if (req_get_val(req, "disabled")) {
		w->attrs &= ~ATTR_DISABLED;

		if (disabled)
			w->attrs |= ATTR_DISABLED;
	}

	if (req_get_val(req, "readonly")) {
		w->attrs &= ~ATTR_READONLY;

		if (readonly)
			w->attrs |= ATTR_READONLY;
	}

	return P_RET_OK;
}

static enum p_retcode compose_set_value(struct request *req, struct widget *root)
{
	struct widget *w = resolve_node(req, root);

	if (!w)
		return P_RET_ERR;

	const char *field = "value";

	if (w->type == WIDGET_CHECKBOX)
		field = "checked";
	else if (w->type == WIDGET_BUTTON)
		field = "clicked";
	else if (w->type == WIDGET_LABEL)
		field = "text";

	struct ipc_pair *pairs = req_data(req);

	for (size_t i = 0; i < pairs->num_kv; i++) {
		const char *key = pairs->kv[i].key;

		if (!streq(key, "action") && !streq(key, "id") &&
		    !streq(key, "node") && !streq(key, "node-id") && !streq(key, field)) {
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
		case WIDGET_LABEL: {
			wchar_t *text __free(ptr) = req_get_wchars(req, "text");

			if (!text || wcslen(text) > COMPOSE_MAX_SIZE)
				break;

			/* Keep the existing geometry: status updates must fit the label. */
			struct widget *candidate = make_label(text);

			if (!candidate)
				break;

			widget_measure_tree(candidate);
			bool fits = candidate->min_w <= w->w && candidate->min_h <= w->h;
			widget_free(candidate);

			if (fits)
				ok = widget_set(w, PROP_TEXT_VALUE, text);

			break;
		}
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
		req_error(req, "unable to set node value: node=%d", w->w_id);
		return P_RET_ERR;
	}

	for (struct widget *parent = w->parent; parent; parent = parent->parent) {
		if (parent->ops && parent->ops->ensure_visible)
			parent->ops->ensure_visible(parent, w);
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

struct button_scan {
	int event;
};

static bool scan_buttons(struct widget *w, void *data)
{
	struct button_scan *scan = data;

	if (w->type != WIDGET_BUTTON)
		return true;

	bool clicked, close;
	widget_get(w, PROP_BUTTON_STATE, &clicked);
	widget_get(w, PROP_BUTTON_CLOSE, &close);

	if (!clicked)
		return true;

	if (close)
		return true;

	scan->event = w->w_id;
	clicked = false;
	widget_set(w, PROP_BUTTON_STATE, &clicked);
	return false;
}

static int compose_take_event(struct widget *root)
{
	struct button_scan scan = { 0 };
	walk_widget_tree(root, scan_buttons, &scan);
	return scan.event;
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
	.p_update_instance    = compose_update,
	.p_set_value_instance = compose_set_value,
	.p_finished           = plugin_buttons_finished,
	.p_take_button_event  = compose_take_event,
	.p_result             = compose_result,
};
