// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/timerfd.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	COMPOSE_SPACER,
	COMPOSE_METER,
	COMPOSE_SPINNER,
	COMPOSE_SPINBOX,
	COMPOSE_TEXTVIEW,
	COMPOSE_COUNT,
};

static const struct {
	const char *name;
	const char *properties[6];
} types[COMPOSE_COUNT] = {
	[COMPOSE_VBOX]     = { "vbox",     { "gap", NULL }                        },
	[COMPOSE_HBOX]     = { "hbox",     { "gap", NULL }                        },
	[COMPOSE_SCROLL]   = { "scroll",   { NULL }                               },
	[COMPOSE_LABEL]    = { "label",    { "text", NULL }                       },
	[COMPOSE_INPUT]    = { "input",    { "value", "max-length", "notify", NULL } },
	[COMPOSE_PASSWORD] = { "password", { "value", "max-length", "notify", NULL } },
	[COMPOSE_CHECKBOX] = { "checkbox", { "checked", "notify", NULL } },
	[COMPOSE_SELECT]   = { "select",   { "option", "visible", "value", "notify", NULL } },
	[COMPOSE_BUTTON]   = { "button",   { "text", "close", NULL }              },
	[COMPOSE_SPACER]   = { "spacer",   { "width", "height", NULL }            },
	[COMPOSE_METER]    = { "meter",    { "total", "value", NULL }             },
	[COMPOSE_SPINNER]  = { "spinner",  { "active", "frames", NULL }           },
	[COMPOSE_SPINBOX]  = { "spinbox",  { "min", "max", "step", "value", "notify", NULL } },
	[COMPOSE_TEXTVIEW] = { "textview", { "text", NULL }                       },
};

struct compose_state {
	struct pollfd timer;
	bool running, finished, failed;
	bool notify[COMPOSE_MAX_NODES + 1];
};

static bool find_active_spinner(struct widget *w, void *data)
{
	if (w->type != WIDGET_SPINNER)
		return true;

	bool active;
	widget_get(w, PROP_SPINNER_ACTIVE, &active);

	if (active) {
		*(bool *) data = true;
		return false;
	}

	return true;
}

static bool compose_sync_timer(struct widget *root)
{
	struct compose_state *st = root->data;
	bool active = false;

	if (!st->finished && (root->flags & FLAG_VISIBLE))
		walk_widget_tree(root, find_active_spinner, &active);

	if (active == st->running)
		return true;

	if (active && st->timer.fd < 0) {
		st->timer.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

		if (st->timer.fd < 0) {
			warn("compose: timerfd_create");
			return false;
		}
	}

	struct itimerspec interval = { 0 };

	if (active) {
		interval.it_value.tv_nsec = 100000000;
		interval.it_interval.tv_nsec = 100000000;
	}

	if (timerfd_settime(st->timer.fd, 0, &interval, NULL) < 0) {
		warn("compose: timerfd_settime");
		return false;
	}

	st->running = active;
	return true;
}

static enum p_retcode compose_delete(struct widget *root)
{
	struct compose_state *st = root->data;

	if (st) {
		if (st->timer.fd >= 0)
			close(st->timer.fd);

		free(st);
		root->data = NULL;
	}

	return P_RET_OK;
}

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
		if (!widget_node_id_valid(name)) {
			req_error(req, "invalid node-id");
			return NULL;
		}

		w = find_widget_by_node_id(root, name);
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

static bool compose_resolve_focus(struct request *req, struct widget *root, struct widget **target)
{
	static const struct req_parameter parameters[] = {
		{ "action",  false },
		{ "id",      false },
		{ "node",    false },
		{ "node-id", false },
		{ NULL,      false },
	};
	*target = NULL;

	if (!req_validate_parameters(req, parameters, "unsupported focus parameter",
				     "duplicate focus parameter"))
		return false;

	if (!req_get_val(req, "node") && !req_get_val(req, "node-id"))
		return true;

	*target = resolve_node(req, root);
	return *target != NULL;
}

static struct widget *create_node(struct request *req, enum compose_type type)
{
	wchar_t *text __free(ptr) = NULL;
	const char *key = NULL;

	switch (type) {
		case COMPOSE_LABEL:
		case COMPOSE_TEXTVIEW:
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
		case COMPOSE_HBOX: {
			int gap;

			if (!read_number(req, "gap", 0, 0, COMPOSE_MAX_SIZE, &gap))
				return NULL;

			if (type == COMPOSE_VBOX)
				w = make_vbox();
			else
				w = make_hbox();

			if (w && widget_set(w, PROP_BOX_GAP, &gap))
				return w;

			break;
		}
		case COMPOSE_SPACER: {
			int width, height;

			if (!read_number(req, "width", 0, 0, COMPOSE_MAX_SIZE, &width) ||
			    !read_number(req, "height", 0, 0, COMPOSE_MAX_SIZE, &height))
				return NULL;

			return make_spacer(width, height);
		}
		case COMPOSE_METER: {
			int total, value;

			if (!read_number(req, "total", 100, 1, INT_MAX, &total) ||
			    !read_number(req, "value", 0, 0, total, &value))
				return NULL;

			w = make_meter(total);

			if (w && widget_set(w, PROP_METER_VALUE, &value))
				return w;

			break;
		}
		case COMPOSE_SPINNER: {
			bool active;
			const char *name = req_get_val(req, "frames");
			static const char *const names[] = { "auto", "ascii", "braille", "wave" };
			enum widget_spinner_frames frames = SPINNER_AUTO;

			if (!req_read_bool(req, "active", false, &active))
				return NULL;

			if (name) {
				for (frames = SPINNER_AUTO; frames <= SPINNER_WAVE; frames++) {
					if (streq(name, names[frames]))
						break;
				}

				if (frames > SPINNER_WAVE) {
					req_error(req, "invalid value: frames");
					return NULL;
				}
			}

			w = make_spinner(frames, active);

			if (w)
				return w;

			break;
		}
		case COMPOSE_SPINBOX: {
			int min, max, step, value;

			if (!read_number(req, "min", 0, INT_MIN, INT_MAX, &min) ||
			    !read_number(req, "max", 100, min, INT_MAX, &max) ||
			    !read_number(req, "step", 1, 1, INT_MAX, &step) ||
			    !read_number(req, "value", min, min, max, &value))
				return NULL;

			char buffer[sizeof(int) * CHAR_BIT + 2];
			int width = snprintf(buffer, sizeof(buffer), "%d", min);
			int max_width = snprintf(buffer, sizeof(buffer), "%d", max);
			return make_spinbox(min, max, step, value, MAX(width, max_width));
		}
		case COMPOSE_SCROLL:
			return make_scroll_vbox();
		case COMPOSE_LABEL:
			return make_label(text);
		case COMPOSE_TEXTVIEW:
			return make_textview(text);
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

	struct compose_state *st = calloc(1, sizeof(*st));

	if (!st) {
		widget_free(root);
		return NULL;
	}

	st->timer.fd = -1;
	st->timer.events = POLLIN;
	root->data = st;

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
		bool disabled, readonly, notify;

		if (!read_number(&node, "flex-w", 0, 0, 256, &flex_w) ||
		    !read_number(&node, "flex-h", count ? 0 : 1, 0, 256, &flex_h) ||
		    !req_read_bool(&node, "disabled", false, &disabled) ||
		    !req_read_bool(&node, "readonly", false, &readonly) ||
		    !req_read_bool(&node, "notify", false, &notify))
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

		if (node_id && (!widget_node_id_valid(node_id) || find_widget_by_node_id(root, node_id))) {
			req_error(req, "invalid or duplicate node-id: %s", node_id);
			goto fail;
		}

		struct widget *w = create_node(&node, type);

		if (!w)
			goto fail;

		if (node_id && !widget_set_node_id(w, node_id)) {
			widget_free(w);
			req_error(req, "no memory");
			goto fail;
		}

		w->w_id = ++count;
		st->notify[count] = notify;
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

	if (!compose_sync_timer(root)) {
		req_error(req, "unable to start compose timer");
		goto fail;
	}

	widget_render_tree(root);
	return root;

fail:
	compose_delete(root);
	widget_free(root);
	return NULL;
}

static enum p_retcode compose_update(struct request *req, struct widget *root)
{
	static const struct req_parameter parameters[] = {
		{ "action",   false },
		{ "id",       false },
		{ "node",     false },
		{ "node-id",  false },
		{ "disabled", false },
		{ "readonly", false },
		{ "option",   true  },
		{ "clear",    false },
		{ "value",    false },
		{ NULL,       false },
	};

	if (!req_validate_parameters(req, parameters, "unknown update parameter",
				     "duplicate update parameter"))
		return P_RET_ERR;

	bool options = req_get_val(req, "option") != NULL;

	bool disabled, readonly, clear;

	if (!req_read_bool(req, "disabled", false, &disabled) ||
	    !req_read_bool(req, "readonly", false, &readonly) ||
	    !req_read_bool(req, "clear", false, &clear))
		return P_RET_ERR;

	struct widget *w = resolve_node(req, root);

	if (!w)
		return P_RET_ERR;

	if ((options || req_get_val(req, "clear") || req_get_val(req, "value")) &&
	    w->type != WIDGET_SELECT) {
		req_error(req, "option, clear and value updates require a select node");
		return P_RET_ERR;
	}

	if ((clear && options) || (req_get_val(req, "value") && !options)) {
		req_error(req, "clear cannot accompany options; value requires options");
		return P_RET_ERR;
	}

	if (!req_get_val(req, "disabled") && !req_get_val(req, "readonly") && !options && !clear) {
		req_error(req, "update requires disabled, readonly, options or clear=true");
		return P_RET_ERR;
	}

	if (options || clear) {
		struct widget *replacement;

		if (clear)
			replacement = make_menu(3);
		else
			replacement = create_node(req, COMPOSE_SELECT);

		if (!replacement)
			return P_RET_ERR;

		bool ok = widget_menu_replace_options(w, replacement);
		widget_free(replacement);

		if (!ok) {
			req_error(req, "replacement options do not fit the select viewport");
			return P_RET_ERR;
		}
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
	else if (w->type == WIDGET_SPINNER)
		field = "active";

	const struct req_parameter parameters[] = {
		{ "action",  false },
		{ "id",      false },
		{ "node",    false },
		{ "node-id", false },
		{ field,     false },
		{ NULL,      false },
	};

	if (!req_validate_parameters(req, parameters, "unknown set-value parameter",
				     "duplicate set-value parameter"))
		return P_RET_ERR;

	if (w->type != WIDGET_BUTTON && !req_get_val(req, field)) {
		req_error(req, "field is missing: %s", field);
		return P_RET_ERR;
	}

	bool ok = false;

	switch (w->type) {
		case WIDGET_TEXTVIEW: {
			wchar_t *text __free(ptr) = req_get_wchars(req, "value");

			if (!text || wcslen(text) > COMPOSE_MAX_SIZE)
				break;

			struct widget *candidate = make_label(text);

			if (!candidate)
				break;

			widget_measure_tree(candidate);
			bool fits = candidate->min_w <= COMPOSE_MAX_SIZE &&
				    candidate->min_h <= COMPOSE_MAX_SIZE &&
				    (size_t) candidate->min_w * (size_t) candidate->min_h <= 1024 * 1024;
			widget_free(candidate);

			if (fits)
				ok = widget_set(w, PROP_TEXT_VALUE, text);

			if (ok) {
				widget_measure_tree(w);
				widget_layout_tree(w, w->lx, w->ly, w->w, w->h);
			}

			break;
		}
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
		case WIDGET_METER: {
			int total, value;

			if (!widget_get(w, PROP_METER_TOTAL, &total) ||
			    !read_number(req, "value", 0, 0, total, &value))
				return P_RET_ERR;

			ok = widget_set(w, PROP_METER_VALUE, &value);
			break;
		}
		case WIDGET_SPINBOX: {
			int min, max, value;

			if (!widget_get(w, PROP_SPINBOX_MIN, &min) ||
			    !widget_get(w, PROP_SPINBOX_MAX, &max) ||
			    !read_number(req, "value", min, min, max, &value))
				return P_RET_ERR;

			ok = widget_set(w, PROP_SPINBOX_VALUE, &value);
			break;
		}
		case WIDGET_SPINNER: {
			bool active, previous;

			if (!req_read_bool(req, "active", false, &active))
				return P_RET_ERR;

			widget_get(w, PROP_SPINNER_ACTIVE, &previous);
			ok = widget_set(w, PROP_SPINNER_ACTIVE, &active);

			if (ok && !compose_sync_timer(root)) {
				widget_set(w, PROP_SPINNER_ACTIVE, &previous);
				req_error(req, "unable to start compose timer");
				return P_RET_ERR;
			}

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

static enum p_retcode compose_get_value(struct request *req, struct widget *root)
{
	static const struct req_parameter parameters[] = {
		{ "action",  false },
		{ "id",      false },
		{ "node",    false },
		{ "node-id", false },
		{ NULL,      false },
	};

	if (!req_validate_parameters(req, parameters, "unknown get-value parameter",
				     "duplicate get-value parameter"))
		return P_RET_ERR;

	struct widget *w = resolve_node(req, root);

	if (!w)
		return P_RET_ERR;

	int number = 0;
	bool state = false, ok = false;

	switch (w->type) {
		case WIDGET_INPUT: {
			wchar_t *text = NULL;

			if (widget_get(w, PROP_INPUT_VALUE, &text) &&
			    ipc_send_string(req_fd(req), "RESPDATA %s VALUE=%ls", req_id(req), text) > 0)
				return P_RET_OK;

			break;
		}
		case WIDGET_SPINBOX:
			ok = widget_get(w, PROP_SPINBOX_VALUE, &number);
			break;
		case WIDGET_METER:
			ok = widget_get(w, PROP_METER_VALUE, &number);
			break;
		case WIDGET_SELECT:
			ok = widget_get(w, PROP_SELECT_CURSOR, &number);
			number++;
			break;
		case WIDGET_CHECKBOX:
			ok = widget_get(w, PROP_CHECKBOX_STATE, &state);
			number = state;
			break;
		case WIDGET_BUTTON:
			ok = widget_get(w, PROP_BUTTON_STATE, &state);
			number = state;
			break;
		case WIDGET_SPINNER:
			ok = widget_get(w, PROP_SPINNER_ACTIVE, &state);
			number = state;
			break;
		default:
			break;
	}

	if (ok && ipc_send_string(req_fd(req), "RESPDATA %s VALUE=%d", req_id(req), number) > 0)
		return P_RET_OK;

	req_error(req, "unable to get node value: node=%d", w->w_id);
	return P_RET_ERR;
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
		case WIDGET_SPINBOX:
			return widget_get(w, PROP_SPINBOX_VALUE, &value) &&
			       ipc_send_string(req_fd(req), "RESPDATA %s SPINBOX_%d=%d", req_id(req), w->w_id, value) > 0;
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

static bool compose_change_token(struct widget *root, struct widget *node, uint64_t *token)
{
	struct compose_state *st = root->data;

	if (node->w_id <= 0 || node->w_id > COMPOSE_MAX_NODES || !st->notify[node->w_id])
		return false;

	int value;

	switch (node->type) {
		case WIDGET_INPUT:
			return widget_get(node, PROP_INPUT_REVISION, token);
		case WIDGET_CHECKBOX: {
			bool checked;

			if (!widget_get(node, PROP_CHECKBOX_STATE, &checked))
				return false;

			*token = checked;
			return true;
		}
		case WIDGET_SELECT:

			if (!widget_get(node, PROP_SELECT_CURSOR, &value))
				return false;

			*token = (uint64_t) value + 1;
			return true;
		case WIDGET_SPINBOX:

			if (!widget_get(node, PROP_SPINBOX_VALUE, &value))
				return false;

			*token = (uint64_t) value;
			return true;
		default:
			return false;
	}
}

static int compose_take_event(struct widget *root)
{
	struct button_scan scan = { 0 };
	walk_widget_tree(root, scan_buttons, &scan);
	return scan.event;
}

static bool compose_finished(struct widget *root)
{
	struct compose_state *st = root->data;

	if (!st->finished && (st->failed || plugin_buttons_finished(root))) {
		st->finished = true;

		if (!compose_sync_timer(root))
			st->failed = true;
	}

	return st->finished;
}

static enum p_retcode compose_visibility_changed(struct widget *root, bool visible)
{
	(void) visible;

	if (compose_sync_timer(root))
		return P_RET_OK;

	struct compose_state *st = root->data;
	st->failed = true;
	return P_RET_ERR;
}

static size_t compose_pollfds(struct widget *root, const struct pollfd **fds)
{
	struct compose_state *st = root->data;
	*fds = NULL;

	if (!st->running)
		return 0;

	*fds = &st->timer;
	return 1;
}

struct spinner_tick {
	uint64_t ticks;
	bool changed;
};

static bool advance_spinner(struct widget *w, void *data)
{
	struct spinner_tick *tick = data;

	if (widget_spinner_advance(w, tick->ticks))
		tick->changed = true;

	return true;
}

static enum p_event_result compose_event(struct widget *root, const struct pollfd *fd)
{
	struct compose_state *st = root->data;
	struct spinner_tick tick = { 0 };

	if (fd->revents & (POLLERR | POLLHUP | POLLNVAL))
		goto fail;

	ssize_t n;

	do {
		n = read(fd->fd, &tick.ticks, sizeof(tick.ticks));
	} while (n < 0 && errno == EINTR);

	if (n < 0 && errno == EAGAIN)
		return P_EVENT_IDLE;

	if (n != sizeof(tick.ticks))
		goto fail;

	walk_widget_tree(root, advance_spinner, &tick);
	return tick.changed ? P_EVENT_REDRAW : P_EVENT_IDLE;

fail:
	st->failed = true;
	return P_EVENT_ERROR;
}

static enum p_retcode compose_result(struct request *req, struct widget *root)
{
	struct compose_state *st = root->data;

	if (st->failed) {
		req_error(req, "compose timer failed");
		return P_RET_ERR;
	}

	return walk_widget_tree(root, collect_result, req) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name                 = "compose",
	.desc                 = "Construct a dialog from a declarative widget tree.",
	.p_create_instance    = compose_create,
	.p_delete_instance    = compose_delete,
	.p_update_instance    = compose_update,
	.p_set_value_instance = compose_set_value,
	.p_get_value_instance = compose_get_value,
	.p_resolve_focus      = compose_resolve_focus,
	.p_finished           = compose_finished,
	.p_take_button_event  = compose_take_event,
	.p_change_token       = compose_change_token,
	.p_result             = compose_result,
	.p_pollfds            = compose_pollfds,
	.p_handle_event       = compose_event,
	.p_visibility_changed = compose_visibility_changed,
};
