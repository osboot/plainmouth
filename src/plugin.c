// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <stdbool.h>
#include <stdio.h>
#include <dirent.h>
#include <dlfcn.h>
#include <err.h>

#include "macros.h"
#include "plugin.h"

LIST_HEAD(plugins, plugin);

static struct plugins plugins = LIST_HEAD_INITIALIZER(plugins);

bool load_plugins(const char *dirpath)
{
	char path[PATH_MAX];

	if (IS_DEBUG())
		warnx("loading plugins ...");

	DIR *dir = opendir(dirpath);
	if (!dir) {
		warn("opendir");
		return false;
	}

	struct dirent *ent;

	while ((ent = readdir(dir)) != NULL) {
		if (!strstr(ent->d_name, ".so"))
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);

		void *handle = dlopen(path, RTLD_NOW);
		if (!handle) {
			warnx("dlopen failed for %s: %s", path, dlerror());
			continue;
		}

		struct plugin *p = dlsym(handle, "plugin");
		if (!p) {
			warnx("no 'plugin' symbol in %s", path);
			dlclose(handle);
			continue;
		}

		if (p->p_plugin_init && p->p_plugin_init() == P_RET_ERR) {
			warnx("initialization failed for plugin '%s'", p->name);
			dlclose(handle);
			continue;
		}

		p->dl_handle = handle;

		LIST_INSERT_HEAD(&plugins, p, entries);

		if (IS_DEBUG())
			warnx("loaded plugin: %s", p->name);
	}

	closedir(dir);

	return true;
}

struct plugin *find_plugin(const char *name)
{
	struct plugin *w;
	LIST_FOREACH(w, &plugins, entries) {
		if (streq(w->name, name))
			return w;
	}
	return NULL;
}

void unload_plugins(void)
{
	struct plugin *w1, *w2;

	LIST_FOREACH(w1, &plugins, entries) {
		if (w1->p_plugin_free && w1->p_plugin_free() == P_RET_ERR)
			warnx("destructor failed for plugin '%s'", w1->name);
	}

	w1 = LIST_FIRST(&plugins);
	while (w1) {
		void *handle = w1->dl_handle;
		w2 = LIST_NEXT(w1, entries);
		dlclose(handle);
		w1 = w2;
	}
}
