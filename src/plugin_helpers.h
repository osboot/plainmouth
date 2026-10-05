// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_PLUGIN_HELPERS_H
#define PLAINMOUTH_PLUGIN_HELPERS_H

#include "plugin.h"

/* The caller owns the container, including partially added buttons on failure.
 * IDs start at 1 and follow the order of button fields in the request. */
bool plugin_add_buttons(struct request *req, struct widget *container);
/* Plugins validate combinations with their own fields before calling this. */
enum p_retcode plugin_set_button(struct request *req, struct widget *root);
bool plugin_buttons_finished(struct widget *root);
/* Ignores non-button widgets and buttons without a positive result ID. */
bool plugin_button_result(struct request *req, struct widget *button);

#endif
