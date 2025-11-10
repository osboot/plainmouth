// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/eventfd.h>
#include <sys/queue.h>

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <getopt.h>
#include <poll.h>
#include <errno.h>
#include <error.h>
#include <err.h>

#include <pthread.h>
#include <ncurses.h>
#include <term.h>

#include "helpers.h"
#include "ipc.h"
#include "plugin.h"
#include "request.h"

/*
 * UI task types — what operations need to be performed in the main thread
 */
enum ui_task_type {
	UI_TASK_NONE = 0,
	UI_TASK_CREATE,
	UI_TASK_UPDATE,
	UI_TASK_DELETE,
	UI_TASK_FOCUS,
	UI_TASK_RESULT,
	UI_TASK_SHOW_SPLASH,
	UI_TASK_HIDE_SPLASH,
};

struct ui_task {
	TAILQ_ENTRY(ui_task) entries;

	enum ui_task_type type;
	uint64_t id;
	struct request req;
	int rc;
};
TAILQ_HEAD(uitasks, ui_task);

struct worker {
	LIST_ENTRY(worker) entries;
	pthread_t thread_id;
};
LIST_HEAD(workers, worker);

struct widget {
	TAILQ_ENTRY(widget) entries;

	char *w_id;
	struct plugin *w_plugin;

	PANEL *w_panel;
	bool w_finished;
};
TAILQ_HEAD(widgets, widget);

static struct workers workers;
static struct widgets widgets;
static struct uitasks uitasks;

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ui_cond  = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t widgets_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  widget_cond   = PTHREAD_COND_INITIALIZER;

static _Atomic uint64_t done_task_id = 0;
static _Atomic uint64_t next_task_id = 1;

static SCREEN *scr = NULL;
static int ui_eventfd = -1;

static _Atomic int do_quit = 0;

static bool use_terminal = true;
static char *debug_file = NULL;

static pthread_t ui_thread;

static const char cmdopts_s[] = "S:Vh";
static const struct option cmdopts[] = {
	{ "debug-file",  required_argument, 0, 1   },
	{ "tty",         required_argument, 0, 2   },
	{ "socket-file", required_argument, 0, 'S' },
	{ "version",     no_argument,       0, 'V' },
	{ "help",        no_argument,       0, 'h' },
	{ 0,             0,                 0, 0   },
};

static void __attribute__((noreturn))
print_help(const char *progname, int retcode)
{
	printf("Usage: %s [options] <socket>\n"
	       "\n"
	       "The plainmouthd daemon is usually run out of the initrd.\n"
	       "It does the heavy lifting of the plainmouth system.\n"
	       "\n"
	       "Options:\n"
	       "   --tty=DEVICE         TTY to use instead of default.\n"
	       "   --debug-file=FILE    File to write debugging information to.\n"
	       "   --socket-file=FILE   Server socket file.\n"
	       "   -V, --version        Show version of program and exit.\n"
	       "   -h, --help           Show this text and exit.\n"
	       "\n",
	       progname);
	exit(retcode);
}

static void __attribute__((noreturn))
print_version(const char *progname)
{
	printf("%s version " PACKAGE_VERSION "\n"
	       "Written by Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "\n"
	       "Copyright (C) 2025  Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "This is free software; see the source for copying conditions. There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	       "\n",
	       progname);
	exit(EXIT_SUCCESS);
}

static struct widget *find_widget(const char *w_id)
{
	struct widget *widget;
	TAILQ_FOREACH(widget, &widgets, entries) {
		if (streq(w_id, widget->w_id))
			return widget;
	}
	return NULL;
}

static void free_widgets(void)
{
	struct widget *w1, *w2;

	TAILQ_FOREACH(w1, &widgets, entries) {
		if (IS_DEBUG())
			warnx("release widget '%s'", w1->w_id);

		if (w1->w_plugin->p_delete_widget && w1->w_plugin->p_delete_widget(w1->w_panel) == P_RET_ERR)
			warnx("destructor failed for widget '%s'", w1->w_id);
	}

	w1 = TAILQ_FIRST(&widgets);
	while (w1) {
		w2 = TAILQ_NEXT(w1, entries);
		free(w1->w_id);
		free(w1);
		w1 = w2;
	}
}

static inline void ui_wakeup(void)
{
	uint64_t one = 1;
	if (write(ui_eventfd, &one, sizeof(one)) < 0 && errno != EAGAIN)
		warn("write(eventfd)");
}

static struct ui_task *ui_task_create(enum ui_task_type type, struct request *req)
{
	if (pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called from UI thread");

	struct ui_task *t = calloc(1, sizeof(*t));
	if (!t) {
		warn("calloc(ui_task)");
		return NULL;
	}

	t->type = type;
	t->req = *req;
	t->rc = 0;
	t->id = next_task_id++;

	return t;
}

/*
 * Queue the task and wait for it to be completed.
 * Returns the field t->rc (0 = ok, < 0 = error).
 */
static int ui_enqueue_and_wait(struct ui_task *t)
{
	if (pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_enqueue_and_wait called from UI thread");

	pthread_mutex_lock(&ui_mutex);
	TAILQ_INSERT_TAIL(&uitasks, t, entries);
	ui_wakeup();
	pthread_mutex_unlock(&ui_mutex);

	pthread_mutex_lock(&ui_mutex);
	while (done_task_id < t->id) {
		pthread_cond_wait(&ui_cond, &ui_mutex);
	}
	pthread_mutex_unlock(&ui_mutex);

	int rc = t->rc;
	free(t);

	return rc;
}

static inline void ui_check_widget_finished(struct widget *w)
{
	if (!w->w_finished && w->w_plugin->p_finished) {
		w->w_finished = w->w_plugin->p_finished(w->w_panel);

		if (w->w_finished) {
			pthread_mutex_lock(&widgets_mutex);
			pthread_cond_broadcast(&widget_cond);
			pthread_mutex_unlock(&widgets_mutex);
		}
	}
}

static struct widget *ui_get_widget_by_id(struct ui_task *t)
{
	const char *widget_id = req_get_val(&t->req, "widget");
	struct widget *widget = find_widget(widget_id);

	if (!widget) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no widget found by id: %s",
				req_id(&t->req), widget_id);
		return NULL;
	}

	return widget;
}

static int ui_process_task_create(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	const char *widget_id = req_get_val(&t->req, "widget");
	struct widget *widget = find_widget(widget_id);

	if (widget) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=widget with '%s' already exists",
				req_id(&t->req), widget_id);
		return -1;
	}

	const char *plugin_name = req_get_val(&t->req, "plugin");
	if (!plugin_name) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=field is missing: plugin",
				req_id(&t->req));
		return -1;
	}

	struct plugin *plugin = find_plugin(plugin_name);
	if (!plugin) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=plugin not found",
				req_id(&t->req));
		return -1;
	}

	struct widget *wnew = calloc(1, sizeof(*wnew));
	if (!wnew) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
				req_id(&t->req));
		return -1;
	}

	wnew->w_id = strdup(widget_id);
	wnew->w_plugin = plugin;

	if (!wnew->w_id) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
				req_id(&t->req));
		free(wnew);
		return -1;
	}

	if (plugin->p_create_widget) {
		wnew->w_panel = plugin->p_create_widget(&t->req);
		if (!wnew->w_panel) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create widget",
					req_id(&t->req));
			free(wnew->w_id);
			free(wnew);
			return -1;
		}
	}

	// A plugin without a callback is always finished.
	wnew->w_finished = (plugin->p_finished == NULL);

	pthread_mutex_lock(&widgets_mutex);
	TAILQ_INSERT_HEAD(&widgets, wnew, entries);
	pthread_mutex_unlock(&widgets_mutex);

	if (use_terminal) {
		update_panels();
		doupdate();
	}

	return 0;
}

static int ui_process_task_update(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct widget *widget = ui_get_widget_by_id(t);
	if (!widget)
		return -1;

	if (widget->w_plugin->p_update_widget &&
			widget->w_plugin->p_update_widget(&t->req, widget->w_panel) != P_RET_OK) {
		return -1;
	}

	ui_check_widget_finished(widget);

	if (use_terminal) {
		update_panels();
		doupdate();
	}

	return 0;
}

static int ui_process_task_delete(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct widget *widget = ui_get_widget_by_id(t);
	if (!widget)
		return -1;

	if (widget->w_plugin->p_delete_widget &&
			widget->w_plugin->p_delete_widget(widget->w_panel) != P_RET_OK) {
		return -1;
	}

	pthread_mutex_lock(&widgets_mutex);
	TAILQ_REMOVE(&widgets, widget, entries);
	pthread_mutex_unlock(&widgets_mutex);

	free(widget->w_id);
	free(widget);

	if (use_terminal) {
		update_panels();
		doupdate();
	}

	return 0;
}

static int ui_process_task_focus(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct widget *widget = ui_get_widget_by_id(t);
	if (!widget)
		return -1;

	pthread_mutex_lock(&widgets_mutex);
	TAILQ_REMOVE(&widgets, widget, entries);
	TAILQ_INSERT_HEAD(&widgets, widget, entries);
	pthread_mutex_unlock(&widgets_mutex);

	top_panel(widget->w_panel);

	if (use_terminal) {
		update_panels();
		doupdate();
	}

	return 0;
}

static int ui_process_task_result(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct widget *widget = ui_get_widget_by_id(t);
	if (!widget)
		return -1;

	if (widget->w_plugin->p_result)
		widget->w_plugin->p_result(&t->req, widget->w_panel);

	return 0;
}

static int ui_process_task_show_splash(struct ui_task *t _UNUSED)
{
	if (!use_terminal) {
		refresh();
		doupdate();
		use_terminal = !use_terminal;
	}
	return 0;
}

static int ui_process_task_hide_splash(struct ui_task *t _UNUSED)
{
	if (use_terminal) {
		endwin();
		use_terminal = !use_terminal;
	}
	return 0;
}

static int ui_process_task_unknown(struct ui_task *t)
{
	ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=unknown action",
			req_id(&t->req));
	return -1;
}


static void ui_process_tasks(void)
{
	struct ui_task *t;

	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_process_tasks called not from UI thread");

	pthread_mutex_lock(&ui_mutex);
	t = TAILQ_FIRST(&uitasks);
	TAILQ_INIT(&uitasks);
	pthread_mutex_unlock(&ui_mutex);

	while (t) {
		int rc;
		struct ui_task *next = TAILQ_NEXT(t, entries);

		switch (t->type) {
			case UI_TASK_CREATE:		rc = ui_process_task_create(t);		break;
			case UI_TASK_UPDATE:		rc = ui_process_task_update(t);		break;
			case UI_TASK_DELETE:		rc = ui_process_task_delete(t);		break;
			case UI_TASK_FOCUS:		rc = ui_process_task_focus(t);		break;
			case UI_TASK_RESULT:		rc = ui_process_task_result(t);		break;
			case UI_TASK_SHOW_SPLASH:	rc = ui_process_task_show_splash(t);	break;
			case UI_TASK_HIDE_SPLASH:	rc = ui_process_task_hide_splash(t);	break;
			case UI_TASK_NONE:		rc = ui_process_task_unknown(t);	break;
		}

		pthread_mutex_lock(&ui_mutex);
		t->rc = rc;
		done_task_id = t->id;
		pthread_cond_broadcast(&ui_cond);
		pthread_mutex_unlock(&ui_mutex);

		t = next;
	}

	if (debug_file)
		fflush(stderr);
}

static int event_loop_iter(void *data __attribute__((unused)))
{
	return do_quit == 0;
}

static int handle_message(struct ipc_ctx *ctx, struct ipc_message *m, void *data __attribute__((unused)))
{
	struct request req = {
		.r_ctx = ctx,
		.r_msg = m,
	};

	const char *action = req_get_val(&req, "action");
	if (!action) {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: action", req_id(&req));
		return -1;
	}

	if (streq(action, "quit")) {
		do_quit = 1;
		ui_wakeup();
		return 0;

	} else if (streq(action, "ping")) {
		ipc_send_string(req_fd(&req), "RESPDATA %s PONG=1", req_id(&req));
		return 0;

	} else if (streq(action, "has-active-vt")) {
		int res = 0;
		if (stdin)
			res = isatty(fileno(stdin));

		ipc_send_string(req_fd(&req), "RESPDATA %s ISTTY=%d", req_id(&req), res);
		return 0;
	}
	else if (streq(action, "wait-result")) {
		const char *widget_id = req_get_val(&req, "widget");
		if (!widget_id) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: widget", req_id(&req));
			return -1;
		}

		struct widget *widget;

		pthread_mutex_lock(&widgets_mutex);
		while (1) {
			widget = find_widget(widget_id);
			if (!widget) {
				pthread_mutex_unlock(&widgets_mutex);
				ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no widget", req_id(&req));
				return -1;
			}
			if (widget->w_finished)
				break;

			pthread_cond_wait(&widget_cond, &widgets_mutex);
		}
		pthread_mutex_unlock(&widgets_mutex);

		struct ui_task *t = ui_task_create(UI_TASK_RESULT, &req);
		if (!t) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no memory", req_id(&req));
			return -1;
		}
		return ui_enqueue_and_wait(t);
	}

	enum ui_task_type ttype = UI_TASK_NONE;

	if (streq(action, "create"))		ttype = UI_TASK_CREATE;
	else if (streq(action, "update"))	ttype = UI_TASK_UPDATE;
	else if (streq(action, "delete"))	ttype = UI_TASK_DELETE;
	else if (streq(action, "focus"))	ttype = UI_TASK_FOCUS;
	else if (streq(action, "result"))	ttype = UI_TASK_RESULT;
	else if (streq(action, "show-splash"))	ttype = UI_TASK_SHOW_SPLASH;
	else if (streq(action, "hide-splash"))	ttype = UI_TASK_HIDE_SPLASH;
	else {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=unknown action", req_id(&req));
		return -1;
	}

	if (ttype != UI_TASK_SHOW_SPLASH && ttype != UI_TASK_HIDE_SPLASH) {
		const char *widget_id = req_get_val(&req, "widget");
		if (!widget_id) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: widget", req_id(&req));
			return -1;
		}
	}

	struct ui_task *t = ui_task_create(ttype, &req);
	if (!t) {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no memory", req_id(&req));
		return -1;
	}

	return ui_enqueue_and_wait(t);
}

static void handle_input(void)
{
	wint_t code;
	int ret = get_wch(&code);

	if (ret == ERR)
		return;

	/* KEYCODE (F1..F12, arrows, HOME, END, PAGEUP etc, including WINCH) */
	if (ret == KEY_CODE_YES) {
		if (code == KEY_RESIZE) {
			int rows, cols;

			getmaxyx(stdscr, rows, cols);
			resize_term(rows, cols);

			if (use_terminal) {
				update_panels();
				doupdate();
			}
			return;
		}
	}

	if (ret == OK) {
		if (code == L'\t') {
			struct widget *w = TAILQ_FIRST(&widgets);

			if (w && TAILQ_NEXT(w, entries)) {
				pthread_mutex_lock(&widgets_mutex);
				TAILQ_REMOVE(&widgets, w, entries);
				TAILQ_INSERT_TAIL(&widgets, w, entries);
				pthread_mutex_unlock(&widgets_mutex);

				w = TAILQ_FIRST(&widgets);
				top_panel(w->w_panel);

				if (use_terminal) {
					update_panels();
					doupdate();
				}
			}
			return;
		}

		struct widget *focused = TAILQ_FIRST(&widgets);

		if (focused && focused->w_plugin->p_input) {
			focused->w_plugin->p_input(focused->w_panel, (wchar_t)code);

			ui_check_widget_finished(focused);

			if (use_terminal) {
				update_panels();
				doupdate();
			}
		}
	}
}

static void handle_tasks(void)
{
	uint64_t val;
	while (read(ui_eventfd, &val, sizeof(val)) > 0);

	ui_process_tasks();
}

static void curses_init(void)
{
	scr = newterm(NULL, stdout, stdin);
	if (!scr)
		errx(EXIT_FAILURE, "newterm failed");

	set_term(scr);

	nodelay(stdscr, TRUE);
	cbreak();
	noecho();
	curs_set(0);

	if (has_colors()) {
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLUE);
		init_pair(2, COLOR_GREEN, COLOR_BLACK);
		init_pair(3, COLOR_CYAN, COLOR_BLACK);
		bkgd((chtype) COLOR_PAIR(1));
	}

	refresh();
}

static void curses_finish(void)
{
	reset_color_pairs();
	endwin();
	delscreen(scr);
}

static void *thread_connection(void *arg)
{
	struct ipc_ctx *ctx = arg;

	ipc_event_loop(ctx);
	ipc_close(ctx);
	free(ctx);

	return NULL;
}

int main(int argc, char **argv)
{
	int c, r, retcode;
	const char *socket_file = NULL;

	while ((c = getopt_long(argc, argv, cmdopts_s, cmdopts, NULL)) != -1) {
		switch (c) {
			case 1:		// --debug-file=Filename
				debug_file = optarg;
				break;
			case 2:		// --tty=TTYDevice
				break;
			case 'S':	// --socket-file=Filename
				socket_file = optarg;
				break;
			case 'V':
				print_version(basename(argv[0]));
				break;
			case 'h':
				print_help(basename(argv[0]), EXIT_SUCCESS);
				break;
		}
	}

	if (!socket_file) {
		socket_file = getenv("PLAINMOUTH_SOCKET");

		if (!socket_file)
			errx(EXIT_FAILURE, "socket file required");
	}

	if (debug_file)
		stderr = freopen(debug_file, "w", stderr);

	setlocale(LC_ALL, "");
	setlocale(LC_CTYPE, "");

	LIST_INIT(&workers);
	TAILQ_INIT(&widgets);
	TAILQ_INIT(&uitasks);

	retcode = EXIT_SUCCESS;

	load_plugins(PLUGINSDIR);

	pthread_attr_t attr;

	r = pthread_attr_init(&attr);
	if (r != 0)
		error(EXIT_FAILURE, r, "pthread_attr_init");

	ui_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
	if (ui_eventfd == -1)
		err(EXIT_FAILURE, "eventfd");

	ui_thread = pthread_self();

	struct ipc_ctx ctx;
	ipc_init(&ctx);

	ctx.event_loop_iter = event_loop_iter;
	ctx.handle_message = handle_message;

	curses_init();
	//atexit(curses_finish);

	ipc_listen(&ctx, socket_file, 42, 0);

	enum {
		POLL_SRVFD   = 0,
		POLL_STDIN   = 1,
		POLL_EVENTFD = 2,
		POLL_N_FDS   = 3,
	};

	struct pollfd pfd[] = {
		[POLL_SRVFD] = {
			.fd = ctx.fd,
			.events = POLLIN,
		},
		[POLL_STDIN] = {
			.fd = fileno(stdin),
			.events = POLLIN,
		},
		[POLL_EVENTFD] = {
			.fd = ui_eventfd,
			.events = POLLIN,
		},
	};

	while (!do_quit) {
		errno = 0;
		r = poll(pfd, POLL_N_FDS, -1);

		if (r < 0) {
			if (errno == EINTR)
				continue;

			warn("poll");

			retcode = EXIT_FAILURE;
			break;
		}

		if (r == 0)
			continue;

		if (pfd[POLL_SRVFD].revents & POLLIN) {
			struct ipc_ctx *client = ipc_accept(&ctx);

			if (client) {
				struct worker *worker = calloc(1, sizeof(*worker));

				r = pthread_create(&worker->thread_id, &attr, &thread_connection, client);
				if (r != 0)
					error(EXIT_FAILURE, r, "pthread_create");

				LIST_INSERT_HEAD(&workers, worker, entries);
			}
		}
		if (pfd[POLL_STDIN].revents & POLLIN) {
			handle_input();
		}
		if (pfd[POLL_EVENTFD].revents & POLLIN) {
			handle_tasks();
		}

		fflush(stderr);
	}

	r = pthread_attr_destroy(&attr);
	if (r != 0)
		error(0, r, "pthread_attr_destroy");

	struct worker *w1 = LIST_FIRST(&workers);
	while (w1 != NULL) {
		struct worker *w2 = LIST_NEXT(w1, entries);

		r = pthread_join(w1->thread_id, NULL);
		if (r != 0)
			error(0, r, "pthread_join");

		free(w1);
		w1 = w2;
	}

	free_widgets();
	unload_plugins();

	ipc_close(&ctx);
	ipc_free(&ctx);

	close(ui_eventfd);
	pthread_mutex_destroy(&ui_mutex);
	pthread_cond_destroy(&ui_cond);

	curses_finish();

	return retcode;
}
