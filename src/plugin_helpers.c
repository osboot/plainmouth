// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "macros.h"
#include "plugin_helpers.h"
#include "widget.h"

struct widget *plugin_create_window(struct request *req, enum plugin_window_layout layout,
				    struct widget **content)
{
	*content = NULL;
	struct widget *root = make_window();
	if (!root)
		return NULL;
	root->style_owner = req->r_style_owner;
	struct widget *parent = root;
	if (req_get_bool(req, "border", false)) {
		switch (layout) {
			case PLUGIN_WINDOW_VERTICAL:
				parent = make_border_vbox(root);
				break;
			case PLUGIN_WINDOW_HORIZONTAL:
				parent = make_border_hbox(root);
				break;
			default:
				parent = NULL;
				break;
		}
		if (!parent) {
			widget_free(root);
			return NULL;
		}
	}
	*content = parent;
	return root;
}

bool plugin_set_indexed_node_id(struct widget *w, const char *prefix, int id)
{
	char name[WIDGET_NODE_ID_MAX + 1];
	int length = snprintf(name, sizeof(name), "%s%d", prefix, id);

	if (id <= 0 || length < 0 || (size_t) length >= sizeof(name))
		return false;

	return widget_set_node_id(w, name);
}

struct widget *plugin_create_button(const wchar_t *label, int id)
{
	struct widget *button = make_button(label);

	if (!button)
		return NULL;

	if (!plugin_set_indexed_node_id(button, "button", id)) {
		widget_free(button);
		return NULL;
	}

	button->w_id = id;
	return button;
}

struct widget *plugin_create_textview(const wchar_t *text)
{
	struct widget *view = make_textview(text);

	if (!view)
		return NULL;

	if (!widget_set_node_id(TAILQ_FIRST(&view->children), "text")) {
		widget_free(view);
		return NULL;
	}

	return view;
}

bool plugin_resolve_focus(struct request *req, struct widget *root, struct widget **target)
{
	static const struct req_parameter parameters[] = {
		{ "action",  false },
		{ "id",      false },
		{ "node-id", false },
		{ NULL,      false },
	};
	*target = NULL;

	if (!req_validate_parameters(req, parameters, "unsupported focus parameter",
				     "duplicate focus parameter"))
		return false;

	const char *name = req_get_val(req, "node-id");

	if (!name)
		return true;

	if (!widget_node_id_valid(name))
		return req_error(req, "invalid node-id");

	*target = find_widget_by_node_id(root, name);

	if (!*target)
		return req_error(req, "node not found");

	return true;
}

struct widget *plugin_create_close_button(struct request *req)
{
	wchar_t *label __free(ptr) = NULL;
	const wchar_t *text = L"OK";
	if (req_get_val(req, "button")) {
		label = req_get_wchars(req, "button");
		if (!label)
			return NULL;
		text = label;
	}
	return plugin_create_button(text, 1);
}

bool plugin_add_buttons(struct request *req, struct widget *container)
{
	struct ipc_pair *pairs = req_data(req);
	int id = 1;

	for (size_t i = 0; i < pairs->num_kv; i++) {
		if (!streq(pairs->kv[i].key, "button"))
			continue;

		wchar_t *label __free(ptr) = req_get_kv_wchars(pairs->kv + i);

		if (!label)
			return false;

		struct widget *button = plugin_create_button(label, id++);

		if (!button) {
			warnx("unable to create button");
			return false;
		}

		widget_add(container, button);
	}

	return true;
}

enum p_retcode plugin_set_button(struct request *req, struct widget *root)
{
	int id;
	bool clicked;

	if (!req_read_int(req, "button", &id) ||
	    !req_read_bool(req, "clicked", true, &clicked))
		return P_RET_ERR;

	struct widget *button = find_widget_by_type_and_id(root, WIDGET_BUTTON, id);

	if (!button) {
		req_error(req, "widget not found: button=%d", id);
		return P_RET_ERR;
	}

	if (!widget_set(button, PROP_BUTTON_STATE, &clicked)) {
		req_error(req, "unable to set value: clicked");
		return P_RET_ERR;
	}

	return P_RET_OK;
}

static bool check_button(struct widget *button, void *data)
{
	bool clicked = false;

	if (button->type == WIDGET_BUTTON && button->w_id > 0)
		widget_get(button, PROP_BUTTON_STATE, &clicked);

	if (clicked)
		*(bool *) data = true;

	return !clicked;
}

bool plugin_buttons_finished(struct widget *root)
{
	bool finished = false;

	walk_widget_tree(root, check_button, &finished);

	return finished;
}

bool plugin_button_result(struct request *req, struct widget *button)
{
	if (button->type != WIDGET_BUTTON || button->w_id <= 0)
		return true;

	bool clicked = false;

	if (!widget_get(button, PROP_BUTTON_STATE, &clicked))
		return false;

	return ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d",
			       req_id(req), button->w_id, clicked) > 0;
}
