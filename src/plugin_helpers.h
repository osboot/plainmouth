// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_PLUGIN_HELPERS_H
#define PLAINMOUTH_PLUGIN_HELPERS_H

#include "plugin.h"

enum plugin_window_layout {
	PLUGIN_WINDOW_VERTICAL,
	PLUGIN_WINDOW_HORIZONTAL,
};

/* Creates a themed window with an optional border. Layout selects the border's
 * inner container; without a border, content is the window itself.
 * The caller owns the returned root; content is borrowed from it.
 * On failure, the partial tree is freed and content is NULL. */
struct widget *plugin_create_window(struct request *req, enum plugin_window_layout layout,
				    struct widget **content);

/* The caller owns this unattached button with ID 1. The first button field
 * supplies its label; if absent, the label is OK. */
struct widget *plugin_create_close_button(struct request *req);
/* Positive result ID; public node ID is buttonN. Caller owns the button. */
struct widget *plugin_create_button(const wchar_t *label, int id);
/* Public node ID is prefixN; preserves the old name on failure. */
bool plugin_set_indexed_node_id(struct widget *w, const char *prefix, int id);
/* Caller owns the textview; its focusable scroll child is publicly named text. */
struct widget *plugin_create_textview(const wchar_t *text);
/* Resolves the common optional node-id selector; NULL means default focus. */
bool plugin_resolve_focus(struct request *req, struct widget *root, struct widget **target);

/* The caller owns the container, including partially added buttons on failure.
 * IDs start at 1 and follow the order of button fields in the request. */
bool plugin_add_buttons(struct request *req, struct widget *container);
/* Plugins validate combinations with their own fields before calling this. */
enum p_retcode plugin_set_button(struct request *req, struct widget *root);
bool plugin_buttons_finished(struct widget *root);
/* Ignores non-button widgets and buttons without a positive result ID. */
bool plugin_button_result(struct request *req, struct widget *button);

#endif
