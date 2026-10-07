// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_INSTANCE_H
#define PLAINMOUTH_DAEMON_INSTANCE_H

#include <sys/queue.h>
#include <stdbool.h>
#include <panel.h>

#include "widget.h"

struct plugin;
struct widget;
struct request;

#define INSTANCE_MAX_EVENTS 256

struct instance_event {
	TAILQ_ENTRY(instance_event)
	entries;
	int node;
	char node_id[WIDGET_NODE_ID_MAX + 1];
	bool change;
};

TAILQ_HEAD(instance_event_queue, instance_event);

struct instance {
	TAILQ_ENTRY(instance)
	entries;
	const char *id;
	struct plugin *plugin;
	struct widget *root;
	PANEL *panel;
	int requested_w, requested_h;
	int requested_x, requested_y;
	bool finished; /* Written by the module; UI-thread readers only. */
	bool events_disabled;
	bool redraw_pending;
	struct instance_event event_pool[INSTANCE_MAX_EVENTS];
	struct instance_event_queue pending_events, free_events;
	size_t event_count;
	bool event_overflow;
	size_t generation;
};
/* All operations and borrowed pointers are UI-thread only, except wait.
 * The module synchronizes list publication, removal and completion with wait.
 * Free all instances after joining workers, before unloading plugins/themes. */
struct instance *daemon_instance_find(const char *id);
struct instance *daemon_instance_first(void);
struct instance *daemon_instance_next(struct instance *instance);
bool daemon_instance_create(struct request *req);
void daemon_instance_delete(struct instance *instance);
void daemon_instances_free(void);
void daemon_instances_resize(void);
void daemon_instance_check_finished(struct instance *instance);
void daemon_instance_input(struct instance *instance, struct widget *node,
			   wchar_t key, bool keycode);
bool daemon_instance_focus(struct instance *instance);
struct widget *daemon_focus_get(void);
void daemon_focus_next(void);
void daemon_focus_prev(void);
void daemon_focus_validate(void);
/* Worker-thread only; returns no instance pointer across the mutex boundary. */
bool daemon_instance_wait(struct request *req);
bool daemon_instance_wait_event(struct request *req);
/* UI-thread only; release current waiters and reject subsequent waits. */
void daemon_instances_stop(void);

#endif
