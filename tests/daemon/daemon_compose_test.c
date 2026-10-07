// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/timerfd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "daemon_instance.h"
#include "plugin.h"
#include "widget.h"

extern struct plugin plugin;

struct plugin *find_plugin(const char *name)
{
	assert(strcmp(name, "compose") == 0);
	return &plugin;
}

static void set_active(struct widget *root, const char *node, bool active)
{
	struct ipc_ctx ctx = { .fd = -1 };
	char id[] = "set";
	struct ipc_message msg = { .id = id };
	struct request req = { .r_ctx = &ctx, .r_msg = &msg };
	assert(ipc_pair_add(&msg.data, "node-id", node));
	assert(ipc_pair_add(&msg.data, "active", active ? "true" : "false"));
	assert(plugin.p_set_value_instance(&req, root) == P_RET_OK);
	ipc_pair_free(&msg.data);
}

static void expect_timer(int fd, bool armed)
{
	struct itimerspec interval;
	assert(timerfd_gettime(fd, &interval) == 0);
	assert((interval.it_interval.tv_nsec != 0) == armed);

	if (!armed)
		assert(interval.it_value.tv_sec == 0 && interval.it_value.tv_nsec == 0);
}

static void check_changes(void)
{
	struct ipc_ctx ctx = { .fd = -1 };
	char id[] = "changes";
	struct ipc_message msg = { .id = id };
	struct request req = { .r_ctx = &ctx, .r_msg = &msg };
	const char *fields[][2] = {
		{ "id",      "changes"  },
		{ "plugin",  "compose"  },
		{ "width",   "32"       },
		{ "height",  "10"       },
		{ "node",    "vbox"     },
		{ "node",    "checkbox" },
		{ "node-id", "check"    },
		{ "notify",  "true"     },
		{ "node",    "end"      },
		{ "node",    "spinbox"  },
		{ "min",     "-10"      },
		{ "max",     "10"       },
		{ "value",   "10"       },
		{ "notify",  "true"     },
		{ "node",    "end"      },
		{ "node",    "select"   },
		{ "option",  "One"      },
		{ "option",  "Two"      },
		{ "value",   "2"        },
		{ "notify",  "true"     },
		{ "node",    "end"      },
		{ "node",    "checkbox" },
		{ "node",    "end"      },
		{ "node",    "button"   },
		{ "text",    "Test"     },
		{ "close",   "false"    },
		{ "node",    "end"      },
		{ "node",    "button"   },
		{ "text",    "OK"       },
		{ "node",    "end"      },
		{ "node",    "end"      },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		assert(ipc_pair_add(&msg.data, fields[i][0], fields[i][1]));

	assert(daemon_instance_create(&req));
	struct instance *ins = daemon_instance_find("changes");
	assert(ins && ins->event_count == 0);
	struct widget *check = find_widget_by_id(ins->root, 2);
	struct widget *spin = find_widget_by_id(ins->root, 3);
	struct widget *select = find_widget_by_id(ins->root, 4);
	assert(check && spin && select);
	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == 1);
	assert(ins->events[0].change && ins->events[0].node == 2);
	assert(strcmp(ins->events[0].node_id, "check") == 0);
	daemon_instance_input(ins, spin, KEY_UP, true);
	assert(ins->event_count == 1);
	daemon_instance_input(ins, spin, KEY_DOWN, true);
	assert(ins->event_count == 2 && ins->events[1].node == 3);
	daemon_instance_input(ins, spin, L'2', false);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, KEY_BACKSPACE, true);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, L'-', false);
	daemon_instance_input(ins, spin, L'2', false);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, L'\n', false);
	assert(ins->event_count == 2 && ins->events[1].node == 3);
	daemon_instance_input(ins, select, KEY_DOWN, true);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, select, KEY_UP, true);
	assert(ins->event_count == 3 && ins->events[2].node == 4);
	check->attrs |= ATTR_READONLY;
	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == 3);
	check->attrs &= ~ATTR_READONLY;
	struct widget *container = find_widget_by_id(ins->root, 1);
	container->attrs |= ATTR_DISABLED;
	daemon_instance_input(ins, spin, KEY_UP, true);
	assert(ins->event_count == 3);
	container->attrs &= ~ATTR_DISABLED;
	daemon_instance_input(ins, find_widget_by_id(ins->root, 5), L' ', false);
	assert(ins->event_count == 3);
	struct ipc_message update = { .id = id };
	struct request setter = { .r_ctx = &ctx, .r_msg = &update };
	assert(ipc_pair_add(&update.data, "node-id", "check"));
	assert(ipc_pair_add(&update.data, "checked", "false"));
	assert(plugin.p_set_value_instance(&setter, ins->root) == P_RET_OK);
	daemon_instance_check_finished(ins);
	assert(ins->event_count == 3 && ins->events[0].node == 2);
	ipc_pair_free(&update.data);
	/* Exercise compaction across the end of the circular buffer. */
	struct instance_event pending[3];
	memcpy(pending, ins->events, sizeof(pending));
	ins->event_head = INSTANCE_MAX_EVENTS - 1;

	for (size_t i = 0; i < 3; i++)
		ins->events[(ins->event_head + i) % INSTANCE_MAX_EVENTS] = pending[i];

	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == 3);
	assert(ins->events[INSTANCE_MAX_EVENTS - 1].node == 3);
	assert(ins->events[0].node == 4 && ins->events[1].node == 2);
	assert(strcmp(ins->events[1].node_id, "check") == 0);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	assert(ins->event_count == 5 && !ins->events[2].change && !ins->events[3].change);
	assert(ins->events[2].node == 6 && ins->events[3].node == 6);

	for (int i = 0; i < INSTANCE_MAX_EVENTS; i++)
		daemon_instance_input(ins, check, L' ', false);

	assert(ins->event_count == 6 && !ins->event_overflow);
	assert(ins->events[1].change && ins->events[4].change);
	assert(ins->events[1].node == 2 && ins->events[4].node == 2);
	/* A duplicate can be replaced even when the queue is full. */
	ins->event_count = INSTANCE_MAX_EVENTS;

	for (size_t i = 0; i < INSTANCE_MAX_EVENTS; i++)
		ins->events[i] = (struct instance_event) { .node = 6 };

	size_t penultimate = (ins->event_head + INSTANCE_MAX_EVENTS - 2) % INSTANCE_MAX_EVENTS;
	size_t last = (ins->event_head + INSTANCE_MAX_EVENTS - 1) % INSTANCE_MAX_EVENTS;
	ins->events[penultimate] = (struct instance_event) { .node = 2, .change = true };
	ins->events[last] = (struct instance_event) { .node = 3, .change = true };
	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && !ins->event_overflow);
	assert(ins->events[penultimate].node == 3 && ins->events[last].node == 2);
	daemon_instance_input(ins, select, KEY_DOWN, true);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && ins->event_overflow);
	daemon_instance_delete(ins);
	ipc_pair_free(&msg.data);
}

int main(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	assert(resize_term(24, 80) == OK);
	struct ipc_ctx ctx = { .fd = -1 };
	char id[] = "create";
	struct ipc_message msg = { .id = id };
	struct request req = { .r_ctx = &ctx, .r_msg = &msg };
	const char *fields[][2] = {
		{ "id",      "spinner" },
		{ "plugin",  "compose" },
		{ "width",   "20"      },
		{ "height",  "5"       },
		{ "border",  "true"    },
		{ "node",    "vbox"    },
		{ "node",    "hbox"    },
		{ "node",    "spinner" },
		{ "node-id", "first"   },
		{ "frames",  "ascii"   },
		{ "node",    "end"     },
		{ "node",    "spinner" },
		{ "node-id", "second"  },
		{ "frames",  "ascii"   },
		{ "node",    "end"     },
		{ "node",    "end"     },
		{ "node",    "button"  },
		{ "text",    "OK"      },
		{ "node",    "end"     },
		{ "node",    "end"     },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		assert(ipc_pair_add(&msg.data, fields[i][0], fields[i][1]));

	assert(daemon_instance_create(&req));
	struct instance *ins = daemon_instance_find("spinner");
	assert(ins);
	const struct pollfd *fds;
	assert(plugin.p_pollfds(ins->root, &fds) == 0 && !fds);
	set_active(ins->root, "first", true);
	assert(plugin.p_pollfds(ins->root, &fds) == 1);
	int fd = fds[0].fd;
	expect_timer(fd, true);
	struct pollfd event = *fds;
	assert(poll(&event, 1, 1000) == 1);
	assert(plugin.p_handle_event(ins->root, &event) == P_EVENT_REDRAW);
	struct widget *first = find_widget_by_id(ins->root, 3);
	struct widget *second = find_widget_by_id(ins->root, 4);
	wchar_t ch;
	assert(widget_get(first, PROP_SPINNER_FRAME, &ch) && ch != L' ');
	assert(widget_get(second, PROP_SPINNER_FRAME, &ch) && ch == L' ');
	set_active(ins->root, "second", true);
	set_active(ins->root, "first", false);
	assert(plugin.p_pollfds(ins->root, &fds) == 1 && fds[0].fd == fd);
	expect_timer(fd, true);
	set_active(ins->root, "second", false);
	assert(plugin.p_pollfds(ins->root, &fds) == 0 && !fds);
	expect_timer(fd, false);
	set_active(ins->root, "first", true);
	assert(resize_term(1, 1) == OK);
	daemon_instances_resize();
	assert(!(ins->root->flags & FLAG_VISIBLE));
	expect_timer(fd, false);
	assert(plugin.p_pollfds(ins->root, &fds) == 0);
	assert(resize_term(24, 80) == OK);
	daemon_instances_resize();
	assert(ins->root->flags & FLAG_VISIBLE);
	expect_timer(fd, true);
	struct widget *button = find_widget_by_id(ins->root, 5);
	bool clicked = true;
	assert(widget_set(button, PROP_BUTTON_STATE, &clicked));
	daemon_instance_check_finished(ins);
	assert(ins->finished);
	expect_timer(fd, false);
	set_active(ins->root, "second", true);
	expect_timer(fd, false);
	daemon_instance_delete(ins);
	errno = 0;
	assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
	assert(daemon_instance_create(&req));
	ins = daemon_instance_find("spinner");
	assert(ins && !ins->finished);
	set_active(ins->root, "first", true);
	assert(plugin.p_pollfds(ins->root, &fds) == 1);
	fd = fds[0].fd;
	expect_timer(fd, true);
	daemon_instance_delete(ins);
	errno = 0;
	assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
	ipc_pair_free(&msg.data);
	check_changes();
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
