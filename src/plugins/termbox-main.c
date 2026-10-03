// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <pthread.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"
#include "termbox-output.h"

struct cleanup_job {
	LIST_ENTRY(cleanup_job)
	entries;
	pid_t pid;
	pthread_t thread;
	atomic_bool done;
};
LIST_HEAD(cleanup_jobs, cleanup_job);
static struct cleanup_jobs cleanup_jobs = LIST_HEAD_INITIALIZER(cleanup_jobs);

struct termbox {
	struct pollfd master;
	struct cleanup_job *child;
	struct termbox_output output;
	struct widget *view, *button;
	bool exited, failed, output_closed;
	int exit_status, exit_signal;
};

static void reap_child(pid_t pid)
{
	int status;

	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		if (errno != ECHILD)
			warn("waitpid");
		break;
	}
}

static void *cleanup_child(void *data)
{
	struct cleanup_job *job = data;

	/* Leave the leader waitable until the group is killed, preventing PID reuse. */
	for (int i = 0; i < 20; i++) {
		siginfo_t info = { 0 };

		if (waitid(P_PID, (id_t) job->pid, &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD)
				goto done;
			break;
		}

		if (info.si_pid)
			break;

		struct timespec delay = { .tv_nsec = 10000000 };

		while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
			;
	}

	if (kill(-job->pid, SIGKILL) < 0 && errno != ESRCH)
		warn("kill process group");

	if (kill(job->pid, SIGKILL) < 0 && errno != ESRCH)
		warn("kill child");

	reap_child(job->pid);
done:
	atomic_store(&job->done, true);
	return NULL;
}

static void cleanup_jobs_collect(bool wait_all)
{
	struct cleanup_job *job = LIST_FIRST(&cleanup_jobs);

	while (job) {
		struct cleanup_job *next = LIST_NEXT(job, entries);

		if (wait_all || atomic_load(&job->done)) {
			int rc = pthread_join(job->thread, NULL);
			if (rc) {
				warnx("pthread_join: %s", strerror(rc));
			} else {
				LIST_REMOVE(job, entries);
				free(job);
			}
		}
		job = next;
	}
}

static enum p_retcode termbox_delete(struct widget *root)
{
	struct termbox *st = root->data;

	if (!st)
		return P_RET_OK;

	if (st->master.fd >= 0)
		close(st->master.fd);

	struct cleanup_job *job = st->child;

	if (job && job->pid > 0) {
		/*
		 * forkpty may not yet have finished creating the child's session.
		 */
		if (kill(-job->pid, SIGHUP) < 0 && errno != ESRCH)
			warn("kill process group");

		if (kill(job->pid, SIGTERM) < 0 && errno != ESRCH)
			warn("kill child");

		int rc = pthread_create(&job->thread, NULL, cleanup_child, job);

		if (!rc) {
			LIST_INSERT_HEAD(&cleanup_jobs, job, entries);
		} else {
			warnx("pthread_create: %s", strerror(rc));

			kill(-job->pid, SIGKILL);
			kill(job->pid, SIGKILL);

			reap_child(job->pid);
			free(job);
		}
	} else {
		free(job);
	}

	free(st);
	root->data = NULL;
	cleanup_jobs_collect(false);

	return P_RET_OK;
}

static enum p_retcode termbox_plugin_free(void)
{
	cleanup_jobs_collect(true);
	return P_RET_OK;
}

static bool termbox_spawn(struct termbox *st, const char *command)
{
	size_t count = 0;

	while (environ[count])
		count++;

	char **environment = calloc(count + 2, sizeof(*environment));

	if (!environment)
		return false;

	size_t n = 0;
	for (size_t i = 0; i < count; i++)
		if (strncmp(environ[i], "TERM=", 5))
			environment[n++] = environ[i];

	char term[] = "TERM=dumb";
	environment[n] = term;

	char shell[] = "sh";
	char option[] = "-c";
	char *args[] = { shell, option, (char *) command, NULL };

	struct sigaction defaults = { .sa_handler = SIG_DFL };

	sigemptyset(&defaults.sa_mask);
	sigset_t empty;
	sigemptyset(&empty);

	struct winsize size = {
		.ws_row = (unsigned short) st->view->h,
		.ws_col = (unsigned short) st->view->w,
	};

	st->child->pid = forkpty(&st->master.fd, NULL, NULL, &size);

	if (st->child->pid == 0) {
		/*
		 * Only syscall/async-signal-safe operations are allowed before exec.
		 */
		if (sigaction(SIGPIPE, &defaults, NULL) || sigaction(SIGHUP,  &defaults, NULL) ||
		    sigaction(SIGINT,  &defaults, NULL) || sigaction(SIGTERM, &defaults, NULL) ||
		    sigaction(SIGCHLD, &defaults, NULL) || sigaction(SIGQUIT, &defaults, NULL) ||
		    sigprocmask(SIG_SETMASK, &empty, NULL))
			_exit(127);

		bool closed = false;

#ifdef HAVE_CLOSE_RANGE
		closed = close_range(3U, UINT_MAX, 0) == 0;
#endif
		if (!closed) {
			struct rlimit limit;

			if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
				_exit(127);

			unsigned long max_fd = INT_MAX;

			if (limit.rlim_max < INT_MAX)
				max_fd = (unsigned long) limit.rlim_max;

			for (unsigned long fd = STDERR_FILENO + 1; fd < max_fd; fd++)
				close((int) fd);
		}

		execve("/bin/sh", args, environment);

		const char error[] = "termbox: unable to execute /bin/sh\n";
		write(STDERR_FILENO, error, sizeof(error) - 1);
		_exit(127);
	}

	int saved_errno = errno;

	free(environment);
	errno = saved_errno;

	if (st->child->pid < 0)
		return false;

	int flags = fcntl(st->master.fd, F_GETFL);

	if (flags < 0 || fcntl(st->master.fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
	    fcntl(st->master.fd, F_SETFD, FD_CLOEXEC) < 0)
		return false;

	st->master.events = POLLIN;

	return true;
}

static struct widget *termbox_create(struct request *req)
{
	cleanup_jobs_collect(false);

	const char *command = req_get_val(req, "command");
	int width, height;

	if (!command || !*command) {
		req_error(req, "field is missing: command");
		return NULL;
	}

	if (!req_read_int(req, "width", &width) || !req_read_int(req, "height", &height))
		return NULL;

	if (width < 3 || height < 4 || width > USHRT_MAX || height > USHRT_MAX) {
		req_error(req, "invalid termbox dimensions");
		return NULL;
	}

	struct widget *root = make_window();
	struct termbox *st = calloc(1, sizeof(*st));

	if (!root || !st) {
		widget_free(root);
		free(st);
		return NULL;
	}

	root->data = st;
	st->master.fd = -1;
	st->child = calloc(1, sizeof(*st->child));

	if (!st->child)
		goto fail;

	atomic_init(&st->child->done, false);
	struct widget *parent = root;

	if (req_get_bool(req, "border", false)) {
		parent = make_border_vbox(root);
		if (!parent)
			goto fail;
	}

	st->view = make_tailview();
	if (!st->view)
		goto fail;

	widget_add(parent, st->view);

	st->button = make_button(L"OK");
	if (!st->button)
		goto fail;

	st->button->w_id = 1;

	widget_add(parent, st->button);

	int x = req_get_int(req, "x", -1);
	int y = req_get_int(req, "y", -1);

	position_center(width, height, &y, &x);

	widget_measure_tree(root);
	widget_layout_tree(root, x, y, width, height);

	if (!termbox_spawn(st, command)) {
		req_error(req, "unable to start command: %s", strerror(errno));
		goto fail;
	}
	widget_render_tree(root);
	return root;
fail:
	termbox_delete(root);
	widget_free(root);
	return NULL;
}

static size_t termbox_pollfds(struct widget *root, const struct pollfd **fds)
{
	struct termbox *st = root->data;
	*fds = &st->master;
	return st->master.fd >= 0 && !st->output_closed ? 1 : 0;
}

static enum p_event_result termbox_event(struct widget *root, const struct pollfd *fd)
{
	struct termbox *st = root->data;

	if (fd->revents & POLLNVAL)
		goto fail;

	size_t remaining = 65536;
	bool changed = false;

	while (remaining) {
		char buffer[4096];
		size_t request = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
		ssize_t n = read(fd->fd, buffer, request);

		if (n > 0) {
			termbox_output_feed(&st->output, buffer, (size_t) n);
			remaining -= (size_t) n;
			changed = true;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EAGAIN && !(fd->revents & (POLLHUP | POLLERR)))
			break;

		/*
		 * Linux PTYs report EIO after the last slave closes.
		 */
		if (!n || (n < 0 && (errno == EIO || (errno == EAGAIN && (fd->revents & POLLHUP))))) {
			termbox_output_finish(&st->output);
			/* Keep the master open: EOF must not send HUP to a live child. */
			st->output_closed = true;
			changed = true;
			break;
		}
		goto fail;
	}

	if (!changed)
		return P_EVENT_IDLE;

	if (!widget_set(st->view, PROP_TEXT_VALUE, st->output.text))
		goto fail;

	return P_EVENT_REDRAW;
fail:
	st->failed = true;
	return P_EVENT_ERROR;
}

static enum p_event_result termbox_child_event(struct widget *root)
{
	struct termbox *st = root->data;

	if (st->exited)
		return P_EVENT_IDLE;

	siginfo_t info = { 0 };

	if (waitid(P_PID, (id_t) st->child->pid, &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
		if (errno == EINTR)
			return P_EVENT_IDLE;
		st->failed = true;
		return P_EVENT_ERROR;
	}

	if (info.si_pid) {
		st->exited = true;
		if (info.si_code == CLD_EXITED)
			st->exit_status = info.si_status;
		else
			st->exit_signal = info.si_status;
	}

	return P_EVENT_IDLE;
}

static bool termbox_finished(struct widget *root)
{
	struct termbox *st = root->data;
	bool clicked = false;

	widget_get(st->button, PROP_BUTTON_STATE, &clicked);

	return st->failed || clicked;
}

static enum p_retcode termbox_result(struct request *req, struct widget *root)
{
	struct termbox *st = root->data;

	if (st->failed) {
		req_error(req, "termbox I/O or child status failed");
		return P_RET_ERR;
	}

	bool clicked = false;

	widget_get(st->button, PROP_BUTTON_STATE, &clicked);
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_1=%d", req_id(req), clicked);
	ipc_send_string(req_fd(req), "RESPDATA %s PID=%ld", req_id(req), (long) st->child->pid);
	ipc_send_string(req_fd(req), "RESPDATA %s RUNNING=%d", req_id(req), !st->exited);
	ipc_send_string(req_fd(req), "RESPDATA %s OUTPUT_CLOSED=%d", req_id(req), st->output_closed);

	if (st->exited) {
		if (st->exit_signal)
			ipc_send_string(req_fd(req), "RESPDATA %s SIGNAL=%d", req_id(req), st->exit_signal);
		else
			ipc_send_string(req_fd(req), "RESPDATA %s EXIT_STATUS=%d", req_id(req), st->exit_status);
	}
	return P_RET_OK;
}

static enum p_retcode termbox_set_value(struct request *req, struct widget *root)
{
	struct termbox *st = root->data;
	int button;
	bool clicked;

	if (!req_read_int(req, "button", &button) || !req_read_bool(req, "clicked", true, &clicked))
		return P_RET_ERR;

	if (button != 1) {
		req_error(req, "widget not found: button=%d", button);
		return P_RET_ERR;
	}
	return widget_set(st->button, PROP_BUTTON_STATE, &clicked) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "termbox",
	.desc = "Run a command in a PTY and display its output.",
	.p_create_instance = termbox_create,
	.p_delete_instance = termbox_delete,
	.p_plugin_free = termbox_plugin_free,
	.p_set_value_instance = termbox_set_value,
	.p_finished = termbox_finished,
	.p_result = termbox_result,
	.p_pollfds = termbox_pollfds,
	.p_handle_event = termbox_event,
	.p_handle_child_event = termbox_child_event,
};
