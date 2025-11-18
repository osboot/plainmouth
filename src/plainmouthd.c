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
#include <curses.h>

#include "helpers.h"
#include "ipc.h"
#include "plugin.h"
#include "request.h"
#include "widget.h"

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

struct instance {
	TAILQ_ENTRY(instance) entries;
	char *id;
	struct plugin *plugin;
	PANEL *panel;
	bool finished;
};
TAILQ_HEAD(instances, instance);

static struct workers workers;
static struct instances instances;
static struct uitasks uitasks;

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ui_cond  = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t instances_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  instance_cond   = PTHREAD_COND_INITIALIZER;

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

static struct instance *find_instance(const char *id)
{
	struct instance *instance;
	TAILQ_FOREACH(instance, &instances, entries) {
		if (streq(id, instance->id))
			return instance;
	}
	return NULL;
}

static void free_instances(void)
{
	struct instance *w1, *w2;

	TAILQ_FOREACH(w1, &instances, entries) {
		if (IS_DEBUG())
			warnx("release instance '%s'", w1->id);

		if (w1->plugin->p_delete_instance && w1->plugin->p_delete_instance(w1->panel) == P_RET_ERR)
			warnx("destructor failed for instance '%s'", w1->id);
	}

	w1 = TAILQ_FIRST(&instances);
	while (w1) {
		w2 = TAILQ_NEXT(w1, entries);
		free(w1->id);
		free(w1);
		w1 = w2;
	}
}

static inline struct instance *instance_focused(void)
{
	return TAILQ_FIRST(&instances);
}

static inline bool instance_can_focus(struct instance *instance)
{
	return instance->plugin->p_input != NULL;
}

static void instance_unfocus(struct instance *instance)
{
	pthread_mutex_lock(&instances_mutex);
	TAILQ_REMOVE(&instances, instance, entries);
	TAILQ_INSERT_TAIL(&instances, instance, entries);
	pthread_mutex_unlock(&instances_mutex);
}

static void instance_infocus(struct instance *instance)
{
	if (!instance_can_focus(instance))
		return;

	if (instance != instance_focused()) {
		pthread_mutex_lock(&instances_mutex);
		TAILQ_REMOVE(&instances, instance, entries);
		TAILQ_INSERT_HEAD(&instances, instance, entries);
		pthread_mutex_unlock(&instances_mutex);
	}

	top_panel(instance->panel);
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

static inline void ui_check_instance_finished(struct instance *w)
{
	if (!w->finished && w->plugin->p_finished) {
		w->finished = w->plugin->p_finished(w->panel);

		if (w->finished) {
			pthread_mutex_lock(&instances_mutex);
			pthread_cond_broadcast(&instance_cond);
			pthread_mutex_unlock(&instances_mutex);
		}
	}
}

static void ui_update_cursor(void)
{
	WINDOW *win;
	int y, x;

	struct instance *focused = instance_focused();
	if (!focused || !focused->panel || !focused->plugin->p_get_cursor) {
		curs_set(0);
		return;
	}

	if (focused->plugin->p_get_cursor(focused->panel, &y, &x) != P_RET_OK) {
		curs_set(0);
		return;
	}

	win = panel_window(focused->panel);
	if (!win) {
		curs_set(0);
		return;
	}

	curs_set(1);

	wmove(win, y, x);
	wnoutrefresh(win);
}

static void ui_update(void)
{
	if (!use_terminal)
		return;

	update_panels();
	ui_update_cursor();
	doupdate();
}

static void ui_next_focused(void)
{
	struct instance *focused = instance_focused();
	if (!focused)
		return;

	struct instance *w;

	for (w = TAILQ_NEXT(focused, entries); w && !instance_can_focus(w); w = TAILQ_NEXT(w, entries));

	if (w) {
		instance_unfocus(focused);
		instance_infocus(w);
		ui_update();
	}
}

static struct instance *ui_get_instance_by_id(struct ui_task *t)
{
	const char *instance_id = req_get_val(&t->req, "id");
	struct instance *instance = find_instance(instance_id);

	if (!instance) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no instance found by id: %s",
				req_id(&t->req), instance_id);
		return NULL;
	}

	return instance;
}

static int ui_process_task_create(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	const char *instance_id = req_get_val(&t->req, "id");
	struct instance *instance = find_instance(instance_id);

	if (instance) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=instance with '%s' already exists",
				req_id(&t->req), instance_id);
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

	struct instance *wnew = calloc(1, sizeof(*wnew));
	if (!wnew) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
				req_id(&t->req));
		return -1;
	}

	wnew->id = strdup(instance_id);
	wnew->plugin = plugin;

	if (!wnew->id) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
				req_id(&t->req));
		free(wnew);
		return -1;
	}

	if (plugin->p_create_instance) {
		wnew->panel = plugin->p_create_instance(&t->req);
		if (!wnew->panel) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create instance",
					req_id(&t->req));
			free(wnew->id);
			free(wnew);
			return -1;
		}
	}

	// A plugin without a callback is always finished.
	wnew->finished = (plugin->p_finished == NULL);

	pthread_mutex_lock(&instances_mutex);

	if (plugin->p_input)
		TAILQ_INSERT_HEAD(&instances, wnew, entries);
	else
		TAILQ_INSERT_TAIL(&instances, wnew, entries);

	pthread_mutex_unlock(&instances_mutex);

	ui_update();

	return 0;
}

static int ui_process_task_update(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	if (instance->plugin->p_update_instance &&
			instance->plugin->p_update_instance(&t->req, instance->panel) != P_RET_OK) {
		return -1;
	}

	ui_check_instance_finished(instance);
	ui_update();

	return 0;
}

static int ui_process_task_delete(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	if (instance->plugin->p_delete_instance &&
			instance->plugin->p_delete_instance(instance->panel) != P_RET_OK) {
		return -1;
	}

	pthread_mutex_lock(&instances_mutex);
	TAILQ_REMOVE(&instances, instance, entries);
	pthread_mutex_unlock(&instances_mutex);

	free(instance->id);
	free(instance);

	ui_update();

	return 0;
}

static int ui_process_task_focus(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	instance_infocus(instance);
	ui_update();

	return 0;
}

static int ui_process_task_result(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	if (instance->plugin->p_result)
		instance->plugin->p_result(&t->req, instance->panel);

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
		const char *instance_id = req_get_val(&req, "id");
		if (!instance_id) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: id", req_id(&req));
			return -1;
		}

		struct instance *instance;

		pthread_mutex_lock(&instances_mutex);
		while (1) {
			instance = find_instance(instance_id);
			if (!instance) {
				pthread_mutex_unlock(&instances_mutex);
				ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no instance", req_id(&req));
				return -1;
			}
			if (instance->finished)
				break;

			pthread_cond_wait(&instance_cond, &instances_mutex);
		}
		pthread_mutex_unlock(&instances_mutex);

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
		const char *instance_id = req_get_val(&req, "id");
		if (!instance_id) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: id", req_id(&req));
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

			ui_update();
			return;
		}
	}

	if (code == L'\t') {
		ui_next_focused();
		return;
	}

	struct instance *focused = instance_focused();

	if (focused && focused->plugin->p_input) {
		focused->plugin->p_input(focused->panel, (wchar_t)code);

		ui_check_instance_finished(focused);

		ui_update();
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

	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	set_escdelay(100);
	curs_set(0);

	if (has_colors()) {
		start_color();
		init_pair(COLOR_PAIR_MAIN,   COLOR_WHITE, COLOR_BLACK);
		init_pair(COLOR_PAIR_WINDOW, COLOR_WHITE, COLOR_BLUE);
		init_pair(COLOR_PAIR_BUTTON, COLOR_BLACK, COLOR_WHITE);
		bkgd(COLOR_PAIR(COLOR_PAIR_MAIN));
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
	TAILQ_INIT(&instances);
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

	free_instances();
	unload_plugins();

	ipc_close(&ctx);
	ipc_free(&ctx);

	close(ui_eventfd);
	pthread_mutex_destroy(&ui_mutex);
	pthread_cond_destroy(&ui_cond);

	curses_finish();

	return retcode;
}
