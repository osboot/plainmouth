// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_PLUGIN_H_
#define _PLAINMOUTH_PLUGIN_H_

#include <sys/queue.h>
#include <stdbool.h>

#include <curses.h>
#include <panel.h>

#include "request.h"

enum p_retcode {
	P_RET_OK  = 0,
	P_RET_ERR = 1,
};

struct plugin {
	LIST_ENTRY(plugin) entries;

	const char *name;
	void *dl_handle;

	enum p_retcode (*p_plugin_init)(void);
	PANEL *(*p_create_widget)(struct request *req);
	enum p_retcode (*p_delete_widget)(PANEL *panel);
	enum p_retcode (*p_update_widget)(struct request *req, PANEL *panel);
	enum p_retcode (*p_input)(PANEL *panel, wchar_t code);
	bool (*p_finished)(PANEL *panel);
	enum p_retcode (*p_result)(struct request *req, PANEL *panel);
	enum p_retcode (*p_plugin_free)(void);
};

bool load_plugins(const char *dirpath);
void unload_plugins(void);

struct plugin *find_plugin(const char *name);

struct widget {
	LIST_ENTRY(widget) entries;

	char *w_id;
	struct plugin *w_plugin;

	PANEL *w_panel;
	bool w_finished;
};

#endif /* _PLAINMOUTH_PLUGIN_H_ */
