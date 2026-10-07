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
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
