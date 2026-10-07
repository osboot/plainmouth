// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_PLUGIN_H_
#define _PLAINMOUTH_PLUGIN_H_

#include <sys/queue.h>
#include <stdbool.h>
#include <poll.h>

#include "request.h"

#define PLUGIN_EXPORT _PUBLIC _USED

enum p_retcode {
	P_RET_OK  = 0,
	P_RET_ERR = 1,
};

enum p_event_result {
	P_EVENT_IDLE,
	P_EVENT_REDRAW,
	P_EVENT_ERROR,
};

struct plugin {
	LIST_ENTRY(plugin) entries;

	const char *name;
	const char *desc;
	void *dl_handle;

	enum p_retcode (*p_plugin_init)(void);
	struct widget *(*p_create_instance)(struct request *req);
	enum p_retcode (*p_delete_instance)(struct widget *root);
	enum p_retcode (*p_update_instance)(struct request *req, struct widget *root);
	enum p_retcode (*p_set_value_instance)(struct request *req, struct widget *root);
	enum p_retcode (*p_get_value_instance)(struct request *req, struct widget *root);
	bool (*p_finished)(struct widget *root);
	/* UI-thread only; consume one pending button event, or return zero. */
	int (*p_take_button_event)(struct widget *root);
	/* UI-thread only; snapshot a node value opted into user change events. */
	bool (*p_change_value)(struct widget *root, struct widget *node, int *value);
	enum p_retcode (*p_result)(struct request *req, struct widget *root);
	enum p_retcode (*p_plugin_free)(void);
	/* Borrowed descriptors; this accessor must not change instance state. */
	size_t (*p_pollfds)(struct widget *root, const struct pollfd **fds);
	enum p_event_result (*p_handle_event)(struct widget *root, const struct pollfd *fd);
	/* SIGCHLD can coalesce: check only this instance's child with WNOHANG. */
	enum p_event_result (*p_handle_child_event)(struct widget *root);
	/* UI-thread notification after the dialog becomes hidden or visible. */
	enum p_retcode (*p_visibility_changed)(struct widget *root, bool visible);
};

bool load_plugins(const char *dirpath);
void unload_plugins(void);

struct plugin *find_plugin(const char *name);
struct plugin *list_plugin(struct plugin *plug);

#endif /* _PLAINMOUTH_PLUGIN_H_ */
