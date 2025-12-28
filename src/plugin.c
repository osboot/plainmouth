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

static int plugin_filter(const struct dirent *ent)
{
	size_t len = strlen(ent->d_name);
	if (len >= 4)
		return streq(ent->d_name + (len - 3), ".so");
	return 0;
}

bool load_plugins(const char *dirpath)
{
	if (IS_DEBUG())
		warnx("loading plugins ...");

	struct dirent **namelist;

	int n = scandir(dirpath, &namelist, plugin_filter, versionsort);
	if (n < 0) {
		warn("scandir");
		return false;
	}

	while (n--) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s", dirpath, namelist[n]->d_name);
		free(namelist[n]);

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
	free(namelist);

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

struct plugin *list_plugin(struct plugin *plug)
{
	if (!plug)
		return LIST_FIRST(&plugins);
	return LIST_NEXT(plug, entries);
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
