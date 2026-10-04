// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_EVENT_H
#define PLAINMOUTH_DAEMON_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <poll.h>

struct instance;

struct daemon_events {
	struct pollfd *fds;
	struct instance **owners;
	size_t count;
};

/* UI-thread only. Arrays are owned by the snapshot; instances are borrowed.
 * Finish dispatch before input/tasks can delete any of its owners.
 * Free each successful snapshot before collecting another one. */
bool daemon_events_collect(const struct pollfd *base, size_t base_count,
			   struct daemon_events *events);
void daemon_events_free(struct daemon_events *events);
void daemon_events_dispatch(struct daemon_events *events);
void daemon_events_handle_children(int fd);
/* Measure, lay out and render pending roots; true requests a screen update. */
bool daemon_events_redraw(void);

#endif
