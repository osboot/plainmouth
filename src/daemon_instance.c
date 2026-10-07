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
static bool stopping;
static size_t next_generation;

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
	struct widget *cur = w->parent;

	while (cur) {
		if (cur->ops && cur->ops->ensure_visible)
			cur->ops->ensure_visible(cur, w);

		cur = cur->parent;
	}
}

static void queue_event(struct instance *instance, int node, bool change, int value)
{
	pthread_mutex_lock(&instances_mutex);

	if (instance->event_count == INSTANCE_MAX_EVENTS) {
		instance->event_overflow = true;
	} else {
		size_t pos = (instance->event_head + instance->event_count++) % INSTANCE_MAX_EVENTS;
		struct instance_event *event = &instance->events[pos];
		*event = (struct instance_event) { .node = node, .change = change, .value = value };
		struct widget *w = find_widget_by_id(instance->root, node);

		if (w && w->node_id)
			snprintf(event->node_id, sizeof(event->node_id), "%s", w->node_id);
	}

	pthread_cond_broadcast(&instance_cond);
	pthread_mutex_unlock(&instances_mutex);
}

void daemon_instance_input(struct instance *instance, struct widget *node,
			   wchar_t key, bool keycode)
{
	int before, after;
	bool notify = instance && !instance->finished && instance->plugin->p_change_value &&
		      instance->plugin->p_change_value(instance->root, node, &before);
	widget_dispatch_input(node, key, keycode);

	if (notify && instance->plugin->p_change_value(instance->root, node, &after) &&
	    before != after)
		queue_event(instance, node->w_id, true, after);

	daemon_instance_check_finished(instance);
}

void daemon_instance_check_finished(struct instance *instance)
{
	if (!instance || instance->finished)
		return;

	if (instance->plugin->p_take_button_event) {
		int node;

		while ((node = instance->plugin->p_take_button_event(instance->root)) > 0) {
			queue_event(instance, node, false, 0);
		}
	}

	if (!instance->plugin->p_finished)
		return;

	bool finished = instance->plugin->p_finished(instance->root);

	if (!finished)
		return;

	pthread_mutex_lock(&instances_mutex);
	instance->finished = true;
	pthread_cond_broadcast(&instance_cond);
	pthread_mutex_unlock(&instances_mutex);
}

static void instance_set_visible(struct instance *ins, bool visible)
{
	bool was_visible = (ins->root->flags & FLAG_VISIBLE) != 0;

	if (visible)
		ins->root->flags |= FLAG_VISIBLE;
	else
		ins->root->flags &= ~FLAG_VISIBLE;

	if (was_visible == visible || !ins->plugin->p_visibility_changed)
		return;

	if (ins->plugin->p_visibility_changed(ins->root, visible) != P_RET_OK) {
		warnx("visibility callback failed for instance '%s'", ins->id);
		ins->events_disabled = true;
		daemon_instance_check_finished(ins);
	}
}

void daemon_instances_resize(void)
{
	struct instance *ins;
	TAILQ_FOREACH(ins, &instances, entries)
	{
		struct widget *root = ins->root;

		widget_measure_tree(root);

		if (COLS < MAX(2, root->min_w) || LINES < MAX(2, root->min_h)) {
			instance_set_visible(ins, false);
			if (hide_panel(ins->panel) == ERR)
				warnx("unable to hide panel of instance '%s'", ins->id);
			continue;
		}

		int width = MIN(ins->requested_w, COLS);
		int height = MIN(ins->requested_h, LINES);
		int x = (COLS - width) / 2;
		int y = (LINES - height) / 2;

		if (ins->requested_x >= 0)
			x = MIN(ins->requested_x, COLS - width);

		if (ins->requested_y >= 0)
			y = MIN(ins->requested_y, LINES - height);

		WINDOW *win = newwin(height, width, y, x);

		if (!win) {
			warnx("unable to resize instance '%s'", ins->id);
			continue;
		}

		if (replace_panel(ins->panel, win) == ERR) {
			warnx("unable to replace panel of instance '%s'", ins->id);
			delwin(win);
			continue;
		}

		/* Destroy derived windows before releasing their backing window. */
		widget_hide_tree(root);
		root->win = win;
		root->flags |= FLAG_CREATED;
		instance_set_visible(ins, true);
		widget_layout_tree(root, x, y, width, height);

		if (focused && streq(focused->instance_id, ins->id))
			widget_ensure_visible(focused);

		widget_render_tree(root);

		if (panel_hidden(ins->panel) && show_panel(ins->panel) == ERR)
			warnx("unable to show panel of instance '%s'", ins->id);
	}
	if (focused) {
		ins = daemon_instance_find(focused->instance_id);
		if (ins && (ins->root->flags & FLAG_VISIBLE))
			top_panel(ins->panel);
	}
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

		focused->flags &= ~(FLAG_INFOCUS | FLAG_REJECTED);
		widget_render_tree(focused);
	}
}

static struct widget *find_focus(struct widget *current, bool reverse)
{
	struct widget *candidate = current;
	struct widget *first = NULL;
	for (;;) {
		if (reverse) {
			candidate = candidate ? TAILQ_PREV(candidate, widgethead, focuses) : NULL;
			if (!candidate)
				candidate = TAILQ_LAST(&focusable, widgethead);
		} else {
			candidate = candidate ? TAILQ_NEXT(candidate, focuses) : NULL;
			if (!candidate)
				candidate = TAILQ_FIRST(&focusable);
		}
		if (!candidate || candidate == first)
			return NULL;
		if (!first)
			first = candidate;
		if (widget_is_interactive(candidate))
			return candidate;
	}
}

void daemon_focus_next(void)
{
	ui_focused(false);
	focused = find_focus(focused, false);
	ui_focused(true);
}

void daemon_focus_prev(void)
{
	ui_focused(false);
	focused = find_focus(focused, true);
	ui_focused(true);
}

void daemon_focus_validate(void)
{
	if (!focused || !widget_is_interactive(focused))
		daemon_focus_next();
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
		wnew->requested_w = wnew->root->w;
		wnew->requested_h = wnew->root->h;
		wnew->requested_x = req_get_int(req, "x", -1);
		wnew->requested_y = req_get_int(req, "y", -1);
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
	wnew->generation = ++next_generation;
	TAILQ_INSERT_TAIL(&instances, wnew, entries);

	pthread_mutex_unlock(&instances_mutex);

	if (!focused)
		focused = find_focus(NULL, false);

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
		if (streq(w->instance_id, instance->id) && widget_is_interactive(w)) {
			ui_focused(false);
			focused = w;
			ui_focused(true);
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
		if (stopping) {
			pthread_mutex_unlock(&instances_mutex);
			req_error(req, "server stopping");
			return false;
		}
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

bool daemon_instance_wait_event(struct request *req)
{
	const char *id = req_get_val(req, "id");
	const char *error = NULL;
	struct instance_event event = { 0 };
	pthread_mutex_lock(&instances_mutex);
	struct instance *initial = daemon_instance_find(id);
	size_t generation = initial ? initial->generation : 0;

	for (;;) {
		struct instance *instance = daemon_instance_find(id);

		if (stopping)
			error = "server stopping";
		else if (!instance || instance->generation != generation)
			error = "no instance";
		else if (!instance->plugin->p_take_button_event && !instance->plugin->p_change_value)
			error = "wait-event is unsupported by plugin";
		else if (instance->event_overflow)
			error = "event queue overflow";

		if (error)
			break;

		if (instance->event_count) {
			event = instance->events[instance->event_head];
			instance->event_head = (instance->event_head + 1) %
					       INSTANCE_MAX_EVENTS;
			instance->event_count--;
			break;
		}

		if (instance->finished) {
			error = "instance finished";
			break;
		}

		pthread_cond_wait(&instance_cond, &instances_mutex);
	}

	pthread_mutex_unlock(&instances_mutex);

	if (error)
		return req_error(req, "%s", error);

	const char *type = "button";

	if (event.change)
		type = "change";

	return ipc_send_string(req_fd(req), "RESPDATA %s EVENT=%s", req_id(req), type) > 0 &&
	       ipc_send_string(req_fd(req), "RESPDATA %s NODE=%d", req_id(req), event.node) > 0 &&
	       (!event.node_id[0] ||
		ipc_send_string(req_fd(req), "RESPDATA %s NODE_ID=%s", req_id(req), event.node_id) > 0) &&
	       (!event.change ||
		ipc_send_string(req_fd(req), "RESPDATA %s VALUE=%d", req_id(req), event.value) > 0);
}

void daemon_instances_stop(void)
{
	pthread_mutex_lock(&instances_mutex);
	stopping = true;
	pthread_cond_broadcast(&instance_cond);
	pthread_mutex_unlock(&instances_mutex);
}
