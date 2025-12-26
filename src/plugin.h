// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_PLUGIN_H_
#define _PLAINMOUTH_PLUGIN_H_

#include <sys/queue.h>
#include <stdbool.h>

#include "request.h"

#define PLUGIN_EXPORT _PUBLIC _USED

enum p_retcode {
	P_RET_OK  = 0,
	P_RET_ERR = 1,
};

struct plugin {
	LIST_ENTRY(plugin) entries;

	const char *name;
	void *dl_handle;

	enum p_retcode (*p_plugin_init)(void);
	struct widget *(*p_create_instance)(struct request *req);
	enum p_retcode (*p_delete_instance)(struct widget *root);
	enum p_retcode (*p_update_instance)(struct request *req, struct widget *root);
	bool (*p_finished)(struct widget *root);
	enum p_retcode (*p_result)(struct request *req, struct widget *root);
	enum p_retcode (*p_plugin_free)(void);
};

bool load_plugins(const char *dirpath);
void unload_plugins(void);

struct plugin *find_plugin(const char *name);

#endif /* _PLAINMOUTH_PLUGIN_H_ */
