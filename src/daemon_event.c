// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/signalfd.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "daemon_event.h"
#include "daemon_instance.h"
#include "plugin.h"
#include "widget.h"

static size_t instance_pollfds(struct instance *ins, const struct pollfd **fds)
{
	*fds = NULL;
	if (ins->finished || ins->events_disabled || !ins->plugin->p_pollfds ||
	    !ins->plugin->p_handle_event)
		return 0;
	return ins->plugin->p_pollfds(ins->root, fds);
}

static bool collect_pollfds(const struct pollfd *base, size_t base_count,
			    struct pollfd **out, struct instance ***owners, size_t *count)
{
	size_t total = base_count;
	struct instance *ins;
	const struct pollfd *fds;
	for (ins = daemon_instance_first(); ins; ins = daemon_instance_next(ins)) {
		size_t n = instance_pollfds(ins, &fds);
		if ((n && !fds) || n > SIZE_MAX / sizeof(**owners) - total ||
		    n > SIZE_MAX / sizeof(**out) - total)
			return false;
		total += n;
	}
	if ((size_t) (nfds_t) total != total)
		return false;
	*out = calloc(total, sizeof(**out));
	*owners = calloc(total, sizeof(**owners));
	if (!*out || !*owners) {
		free(*out);
		free(*owners);
		return false;
	}
	memcpy(*out, base, base_count * sizeof(**out));
	size_t pos = base_count;
	for (ins = daemon_instance_first(); ins; ins = daemon_instance_next(ins)) {
		size_t n = instance_pollfds(ins, &fds);
		if ((n && !fds) || n > total - pos) {
			free(*out);
			free(*owners);
			return false;
		}
		for (size_t i = 0; i < n; i++, pos++) {
			(*out)[pos] = fds[i];
			(*out)[pos].revents = 0;
			(*owners)[pos] = ins;
		}
	}
	*count = total;
	return true;
}

static void plugin_event_result(struct instance *ins, enum p_event_result result)
{
	if (result == P_EVENT_ERROR) {
		warnx("event handler failed for instance '%s'", ins->id);
		ins->events_disabled = true;
	}

	if (result == P_EVENT_REDRAW)
		ins->redraw_pending = true;

	daemon_instance_check_finished(ins);
}

void daemon_events_dispatch(struct daemon_events *events)
{
	struct pollfd *fds = events->fds;
	struct instance **owners = events->owners;
	size_t count = events->count;
	for (size_t i = 0; i < count; i++) {
		struct instance *ins = owners[i];
		if (!ins || !fds[i].revents || ins->finished || ins->events_disabled)
			continue;
		plugin_event_result(ins, ins->plugin->p_handle_event(ins->root, &fds[i]));
	}
}

void daemon_events_handle_children(int fd)
{
	struct signalfd_siginfo info;
	bool pending = false;

	for (;;) {
		ssize_t n = read(fd, &info, sizeof(info));

		if (n == sizeof(info)) {
			pending = true;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EAGAIN)
			break;

		warnx("unable to read child signal notification");
		break;
	}

	if (!pending)
		return;

	struct instance *ins;

	for (ins = daemon_instance_first(); ins; ins = daemon_instance_next(ins)) {
		if (!ins->finished && !ins->events_disabled && ins->plugin->p_handle_child_event)
			plugin_event_result(ins, ins->plugin->p_handle_child_event(ins->root));
	}
}

bool daemon_events_redraw(void)
{
	bool redraw = false;
	struct instance *ins;
	for (ins = daemon_instance_first(); ins; ins = daemon_instance_next(ins)) {
		if (!ins->redraw_pending)
			continue;

		struct widget *root = ins->root;

		widget_measure_tree(root);
		widget_layout_tree(root, root->lx, root->ly, root->w, root->h);
		widget_render_tree(root);

		ins->redraw_pending = false;
		redraw = true;
	}
	return redraw;
}

bool daemon_events_collect(const struct pollfd *base, size_t base_count,
			   struct daemon_events *events)
{
	*events = (struct daemon_events) { 0 };
	struct pollfd *fds;
	struct instance **owners;
	size_t count;
	if (!collect_pollfds(base, base_count, &fds, &owners, &count))
		return false;
	events->fds = fds;
	events->owners = owners;
	events->count = count;
	return true;
}

void daemon_events_free(struct daemon_events *events)
{
	free(events->fds);
	free(events->owners);
	*events = (struct daemon_events) { 0 };
}
