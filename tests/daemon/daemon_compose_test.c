// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/timerfd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

static struct instance_event *event_at(struct instance *ins, size_t index)
{
	struct instance_event *event = TAILQ_FIRST(&ins->pending_events);

	while (event && index > 0) {
		event = TAILQ_NEXT(event, entries);
		index--;
	}

	assert(event);
	return event;
}

static void check_event_pool(struct instance *ins)
{
	struct instance_event *event;
	size_t pending = 0, available = 0, reverse = 0;

	TAILQ_FOREACH(event, &ins->pending_events, entries)
	{
		pending++;
	}

	TAILQ_FOREACH(event, &ins->free_events, entries)
	{
		available++;
	}

	TAILQ_FOREACH_REVERSE(event, &ins->pending_events, instance_event_queue, entries)
	{
		reverse++;
	}

	assert(pending == ins->event_count && reverse == pending);
	assert(pending + available == INSTANCE_MAX_EVENTS);
}

static void consume_event(struct instance *ins)
{
	struct instance_event *pending = event_at(ins, 0);
	char node[32], node_id[WIDGET_NODE_ID_MAX + 9];
	snprintf(node, sizeof(node), "NODE=%d", pending->node);
	snprintf(node_id, sizeof(node_id), "NODE_ID=%s", pending->node_id);
	const char *fields[] = { pending->change ? "EVENT=change" : "EVENT=button", node, node_id };
	size_t count = pending->node_id[0] ? 3 : 2;
	int fd[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
	struct ipc_ctx ctx = { .fd = fd[0] }, receiver;
	char id[] = "event";
	struct ipc_message msg = { .id = id };
	struct request req = { .r_ctx = &ctx, .r_msg = &msg };
	assert(ipc_pair_add(&msg.data, "id", ins->id));
	assert(daemon_instance_wait_event(&req));
	assert(TAILQ_FIRST(&ins->free_events) == pending);
	assert(close(fd[0]) == 0);
	ipc_init(&receiver);
	receiver.fd = fd[1];

	for (size_t i = 0; i < count; i++) {
		struct ipc_token tok = { 0 };
		assert(ipc_recv_token(&receiver, &tok) > 0);
		assert(strcmp(tok.cmd, "RESPDATA") == 0 && strcmp(tok.id, id) == 0);
		assert(strcmp(tok.arg, fields[i]) == 0);
		ipc_free_token(&tok);
	}

	assert(receiver.inbuf.len == 0);
	char extra;
	assert(ipc_recv_data(receiver.fd, &extra, sizeof(extra)) == 0);
	ipc_free(&receiver);
	ipc_pair_free(&msg.data);
	check_event_pool(ins);
}

static void clear_events(struct instance *ins)
{
	while (ins->event_count)
		consume_event(ins);
}

static void expect_input_key(struct instance *ins, struct widget *w, wchar_t key,
			     bool keycode, const wchar_t *expected, bool changed)
{
	clear_events(ins);
	daemon_instance_input(ins, w, key, keycode);
	wchar_t *text;
	assert(widget_get(w, PROP_INPUT_VALUE, &text));
	assert(wcscmp(text, expected) == 0);
	assert(ins->event_count == (changed ? 1U : 0U));

	if (changed) {
		struct instance_event *event = event_at(ins, 0);
		assert(event->change && event->node == w->w_id);
		assert(strcmp(event->node_id, w->node_id) == 0);
	}

	check_event_pool(ins);
}

static void check_text_changes(void)
{
	const char *types[] = { "input", "password" };

	for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
		struct ipc_ctx ctx = { .fd = -1 };
		char id[] = "text-changes";
		struct ipc_message msg = { .id = id };
		struct request req = { .r_ctx = &ctx, .r_msg = &msg };
		const char *fields[][2] = {
			{ "id",         id        },
			{ "plugin",     "compose" },
			{ "width",      "32"      },
			{ "height",     "10"      },
			{ "node",       "vbox"    },
			{ "node",       types[i]  },
			{ "node-id",    "text"    },
			{ "notify",     "true"    },
			{ "max-length", "3"       },
			{ "value",      "ab"      },
			{ "node",       "end"     },
			{ "node",       types[i]  },
			{ "node-id",    "silent"  },
			{ "value",      ""        },
			{ "node",       "end"     },
			{ "node",       "button"  },
			{ "text",       "OK"      },
			{ "node",       "end"     },
			{ "node",       "end"     },
		};

		for (size_t j = 0; j < sizeof(fields) / sizeof(*fields); j++)
			assert(ipc_pair_add(&msg.data, fields[j][0], fields[j][1]));

		assert(daemon_instance_create(&req));
		struct instance *ins = daemon_instance_find(id);
		assert(ins && ins->event_count == 0);
		struct widget *w = find_widget_by_id(ins->root, 2);
		assert(w);
		expect_input_key(ins, w, KEY_LEFT, true, L"ab", false);
		expect_input_key(ins, w, L'x', false, L"axb", true);
		expect_input_key(ins, w, L'y', false, L"axb", false);
		expect_input_key(ins, w, KEY_DC, true, L"ax", true);
		expect_input_key(ins, w, KEY_DC, true, L"ax", false);
		expect_input_key(ins, w, KEY_BACKSPACE, true, L"a", true);
		expect_input_key(ins, w, KEY_HOME, true, L"a", false);
		expect_input_key(ins, w, KEY_BACKSPACE, true, L"a", false);
		expect_input_key(ins, w, KEY_RIGHT, true, L"a", false);
		expect_input_key(ins, w, KEY_END, true, L"a", false);
		expect_input_key(ins, w, KEY_ENTER, true, L"a", false);
		expect_input_key(ins, w, L'\t', false, L"a", false);
		expect_input_key(ins, w, KEY_F(1), true, L"a", false);
		expect_input_key(ins, w, L'\u00e9', false, L"a\u00e9", true);
		expect_input_key(ins, w, 127, false, L"a", true);
		expect_input_key(ins, w, L'\b', false, L"", true);
		expect_input_key(ins, w, L'\b', false, L"", false);
		w->attrs |= ATTR_READONLY;
		expect_input_key(ins, w, L'x', false, L"", false);
		w->attrs &= ~ATTR_READONLY;
		struct widget *container = find_widget_by_id(ins->root, 1);
		container->attrs |= ATTR_DISABLED;
		expect_input_key(ins, w, L'x', false, L"", false);
		container->attrs &= ~ATTR_DISABLED;
		expect_input_key(ins, find_widget_by_id(ins->root, 3), L'x', false, L"x", false);

		struct ipc_message update = { .id = id };
		struct request setter = { .r_ctx = &ctx, .r_msg = &update };
		assert(ipc_pair_add(&update.data, "node-id", "text"));
		assert(ipc_pair_add(&update.data, "value", "abc"));
		assert(plugin.p_set_value_instance(&setter, ins->root) == P_RET_OK);
		daemon_instance_check_finished(ins);
		assert(ins->event_count == 0);
		ipc_pair_free(&update.data);
		expect_input_key(ins, w, L'd', false, L"abc", false);
		expect_input_key(ins, w, KEY_BACKSPACE, true, L"ab", true);

		/* Repeated edits remain one notification even if the text is restored. */
		daemon_instance_input(ins, w, L'c', false);
		daemon_instance_input(ins, w, KEY_BACKSPACE, true);
		assert(ins->event_count == 1 && !ins->event_overflow);
		daemon_instance_delete(ins);
		ipc_pair_free(&msg.data);
	}
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
	assert(event_at(ins, 0)->change && event_at(ins, 0)->node == 2);
	assert(strcmp(event_at(ins, 0)->node_id, "check") == 0);
	daemon_instance_input(ins, spin, KEY_UP, true);
	assert(ins->event_count == 1);
	daemon_instance_input(ins, spin, KEY_DOWN, true);
	assert(ins->event_count == 2 && event_at(ins, 1)->node == 3);
	daemon_instance_input(ins, spin, L'2', false);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, KEY_BACKSPACE, true);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, L'-', false);
	daemon_instance_input(ins, spin, L'2', false);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, spin, L'\n', false);
	assert(ins->event_count == 2 && event_at(ins, 1)->node == 3);
	daemon_instance_input(ins, select, KEY_DOWN, true);
	assert(ins->event_count == 2);
	daemon_instance_input(ins, select, KEY_UP, true);
	assert(ins->event_count == 3 && event_at(ins, 2)->node == 4);
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
	assert(ins->event_count == 3 && event_at(ins, 0)->node == 2);
	ipc_pair_free(&update.data);
	struct instance_event *check_event = event_at(ins, 0);
	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == 3);
	assert(event_at(ins, 0)->node == 3);
	assert(event_at(ins, 1)->node == 4 && event_at(ins, 2) == check_event);
	assert(strcmp(check_event->node_id, "check") == 0);
	check_event_pool(ins);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	assert(ins->event_count == 5 && !event_at(ins, 3)->change && !event_at(ins, 4)->change);
	assert(event_at(ins, 3)->node == 6 && event_at(ins, 4)->node == 6);

	for (int i = 0; i < INSTANCE_MAX_EVENTS; i++)
		daemon_instance_input(ins, check, L' ', false);

	assert(ins->event_count == 6 && !ins->event_overflow);
	assert(event_at(ins, 2)->change && event_at(ins, 5)->change);
	assert(event_at(ins, 2)->node == 2 && event_at(ins, 5)->node == 2);
	check_event_pool(ins);
	clear_events(ins);
	assert(TAILQ_EMPTY(&ins->pending_events));

	/* Reuse a named event's slot for an unnamed button without leaking its ID. */
	daemon_instance_input(ins, check, L' ', false);
	check_event = event_at(ins, 0);
	consume_event(ins);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	assert(event_at(ins, 0) == check_event && !check_event->node_id[0]);
	consume_event(ins);

	/* A duplicate can be replaced even when the queue is full. */

	for (size_t i = 0; i < INSTANCE_MAX_EVENTS - 2; i++)
		daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);

	daemon_instance_input(ins, check, L' ', false);
	daemon_instance_input(ins, spin, KEY_DOWN, true);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && !ins->event_overflow);
	assert(TAILQ_EMPTY(&ins->free_events));
	check_event = event_at(ins, INSTANCE_MAX_EVENTS - 2);
	daemon_instance_input(ins, check, L' ', false);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && !ins->event_overflow);
	assert(event_at(ins, INSTANCE_MAX_EVENTS - 2)->node == 3);
	assert(event_at(ins, INSTANCE_MAX_EVENTS - 1) == check_event);
	check_event_pool(ins);
	consume_event(ins);
	assert(ins->event_count == INSTANCE_MAX_EVENTS - 1);
	daemon_instance_input(ins, select, KEY_DOWN, true);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && !ins->event_overflow);
	assert(event_at(ins, INSTANCE_MAX_EVENTS - 1)->node == 4);
	daemon_instance_input(ins, find_widget_by_id(ins->root, 6), L'\n', false);
	assert(ins->event_count == INSTANCE_MAX_EVENTS && ins->event_overflow);
	check_event_pool(ins);
	daemon_instance_delete(ins);
	ipc_pair_free(&msg.data);
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
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
	check_text_changes();
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
