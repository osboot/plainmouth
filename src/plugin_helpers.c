// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdlib.h>
#include <err.h>

#include "macros.h"
#include "plugin_helpers.h"
#include "widget.h"

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

		struct widget *button = make_button(label);

		if (!button) {
			warnx("unable to create button");
			return false;
		}

		button->w_id = id++;
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
