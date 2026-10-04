// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <pthread.h>
#include <curses.h>
#include <panel.h>

#include "daemon_instance.h"
#include "daemon_style.h"
#include "macros.h"
#include "plugin.h"
#include "request.h"
#include "widget.h"

TAILQ_HEAD(instances, instance);
static struct instances instances = TAILQ_HEAD_INITIALIZER(instances);
static struct widgethead focusable = TAILQ_HEAD_INITIALIZER(focusable);
static struct widget *focused;
static pthread_mutex_t instances_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t instance_cond = PTHREAD_COND_INITIALIZER;

struct instance *daemon_instance_find(const char *id)
{
	if (!id)
		return NULL;

	struct instance *instance;
	TAILQ_FOREACH(instance, &instances, entries)
	{
		if (streq(instance->id, id))
			return instance;
	}
	return NULL;
}

static void use_instance_widgets(struct instance *ins, struct widget *w)
{
	struct widget *child;

	TAILQ_FOREACH_REVERSE(child, &w->children, widgethead, siblings)
	{
		use_instance_widgets(ins, child);
	}

	w->instance_id = ins->id;

	if (w->attrs & ATTR_CAN_FOCUS) {
		TAILQ_INSERT_HEAD(&focusable, w, focuses);
	}
}

static void release_instance(struct instance *instance)
{
	if (IS_DEBUG())
		warnx("release instance '%s'", instance->id);

	TAILQ_REMOVE(&instances, instance, entries);

	struct widget *w1 = TAILQ_FIRST(&focusable);
	while (w1) {
		struct widget *w2 = TAILQ_NEXT(w1, focuses);
		if (streq(w1->instance_id, instance->id)) {
			if (w1 == focused)
				focused = NULL;
			TAILQ_REMOVE(&focusable, w1, focuses);
		}
		w1 = w2;
	}

	if (instance->panel) {
		if (IS_DEBUG())
			warnx("destroy panel of instance '%s'", instance->id);
		if (del_panel(instance->panel) == ERR)
			warnx("unable to destroy panel of instance '%s'", instance->id);
		instance->panel = NULL;
	}

	if (instance->root) {
		if (instance->plugin && instance->plugin->p_delete_instance &&
		    instance->plugin->p_delete_instance(instance->root) != P_RET_OK) {
			warnx("plugin delete callback failed for instance '%s'", instance->id);
		}

		widget_free(instance->root);
		instance->root = NULL;
	}

	free((char *) instance->id);
	free(instance);
}

void daemon_instances_free(void)
{
	while (!TAILQ_EMPTY(&instances))
		release_instance(TAILQ_FIRST(&instances));
}

static void widget_ensure_visible(struct widget *w)
{
	struct widget *child = w;
	struct widget *cur = w->parent;

	while (cur) {
		if (cur->ops && cur->ops->ensure_visible)
			cur->ops->ensure_visible(cur, child);

		child = cur;
		cur = cur->parent;
	}
}

void daemon_instance_check_finished(struct instance *instance)
{
	if (!instance || instance->finished || !instance->plugin->p_finished)
		return;
	bool finished = instance->plugin->p_finished(instance->root);
	if (!finished)
		return;
	pthread_mutex_lock(&instances_mutex);
	instance->finished = true;
	pthread_cond_broadcast(&instance_cond);
	pthread_mutex_unlock(&instances_mutex);
}

static void ui_focused(bool state)
{
	if (!focused)
		return;

	if (state) {
		if (IS_DEBUG())
			warnx("%s (%p) in focus", widget_type(focused), focused->win);

		focused->flags |= FLAG_INFOCUS;

		widget_ensure_visible(focused);

		/*
		 * This is necessary to ensure that the panel with the widget
		 * in focus is on top of everything else.
		 */
		struct instance *ins = daemon_instance_find(focused->instance_id);
		widget_render_tree(ins->root);
		top_panel(ins->panel);
	} else {
		if (IS_DEBUG())
			warnx("%s (%p) lost focus", widget_type(focused), focused->win);

		focused->flags &= ~FLAG_INFOCUS;
		widget_render_tree(focused);
	}
}

void daemon_focus_next(void)
{
	if (focused) {
		ui_focused(false);
		focused = TAILQ_NEXT(focused, focuses);
	}
	if (!focused)
		focused = TAILQ_FIRST(&focusable);
	if (focused) {
		ui_focused(true);
	}
}

bool daemon_instance_create(struct request *req)
{
	const char *instance_id = req_get_val(req, "id");
	struct instance *instance = daemon_instance_find(instance_id);

	if (instance) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=instance with '%s' already exists",
				req_id(req), instance_id);
		return false;
	}

	const char *plugin_name = req_get_val(req, "plugin");
	if (!plugin_name) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=field is missing: plugin",
				req_id(req));
		return false;
	}

	struct plugin *plugin = find_plugin(plugin_name);
	if (!plugin) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=plugin not found",
				req_id(req));
		return false;
	}

	const char *style_name = req_get_val(req, "style");
	if (style_name) {
		struct widget *style = daemon_style_find(style_name);
		if (!style) {
			req_error(req, "unknown style: %s", style_name);
			return false;
		}
		req->r_style_owner = style;
	}

	struct instance *wnew = calloc(1, sizeof(*wnew));
	if (!wnew) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=no memory",
				req_id(req));
		return false;
	}

	wnew->id = strdup(instance_id);
	if (!wnew->id) {
		req_error(req, "no memory");
		free(wnew);
		return false;
	}
	wnew->plugin = plugin;

	if (plugin->p_create_instance) {
		wnew->root = plugin->p_create_instance(req);
		if (!wnew->root) {
			ipc_send_string(req_fd(req),
					"RESPDATA %s ERR=unable to create instance",
					req_id(req));
			free((char *) wnew->id);
			free(wnew);
			return false;
		}

		wnew->panel = new_panel(wnew->root->win);
		if (!wnew->panel) {
			ipc_send_string(req_fd(req),
					"RESPDATA %s ERR=unable to create panel",
					req_id(req));
			if (wnew->plugin && wnew->plugin->p_delete_instance &&
			    wnew->plugin->p_delete_instance(wnew->root) != P_RET_OK) {
				warnx("plugin delete callback failed for instance '%s'", wnew->id);
			}
			widget_free(wnew->root);
			free((char *) wnew->id);
			free(wnew);
			return false;
		}
	}

	// A plugin without a callback is always finished.
	wnew->finished = (plugin->p_finished == NULL);

	pthread_mutex_lock(&instances_mutex);

	use_instance_widgets(wnew, wnew->root);
	TAILQ_INSERT_TAIL(&instances, wnew, entries);

	pthread_mutex_unlock(&instances_mutex);

	if (!focused)
		focused = TAILQ_FIRST(&focusable);

	ui_focused(true);

	return true;
}

struct widget *daemon_focus_get(void)
{
	return focused;
}

struct instance *daemon_instance_first(void)
{
	return TAILQ_FIRST(&instances);
}

struct instance *daemon_instance_next(struct instance *instance)
{
	return TAILQ_NEXT(instance, entries);
}

void daemon_instance_delete(struct instance *instance)
{
	pthread_mutex_lock(&instances_mutex);
	release_instance(instance);
	pthread_cond_broadcast(&instance_cond);
	pthread_mutex_unlock(&instances_mutex);
}

bool daemon_instance_focus(struct instance *instance)
{
	struct widget *w;
	TAILQ_FOREACH(w, &focusable, focuses)
	{
		if (streq(w->instance_id, instance->id)) {
			focused = w;
			top_panel(instance->panel);
			return true;
		}
	}
	return false;
}

bool daemon_instance_wait(struct request *req)
{
	const char *id = req_get_val(req, "id");
	pthread_mutex_lock(&instances_mutex);
	for (;;) {
		struct instance *instance = daemon_instance_find(id);
		if (!instance) {
			pthread_mutex_unlock(&instances_mutex);
			req_error(req, "no instance");
			return false;
		}
		if (instance->finished)
			break;
		pthread_cond_wait(&instance_cond, &instances_mutex);
	}
	pthread_mutex_unlock(&instances_mutex);
	return true;
}
