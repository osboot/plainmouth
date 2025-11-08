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

struct worker {
	LIST_ENTRY(worker) entries;
	pthread_t thread_id;
};

LIST_HEAD(workers, worker);
LIST_HEAD(widgets, widget);
TAILQ_HEAD(uitasks, ui_task);

static struct workers workers;
static struct widgets widgets;
static struct uitasks uitasks;

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ui_cond  = PTHREAD_COND_INITIALIZER;

static _Atomic uint64_t done_task_id = 0;
static _Atomic uint64_t next_task_id = 1;

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
	LIST_FOREACH(widget, &widgets, entries) {
		if (streq(w_id, widget->w_id))
			return widget;
	}
	return NULL;
}

static void free_widgets(void)
{
	struct widget *w1, *w2;

	LIST_FOREACH(w1, &widgets, entries) {
		if (IS_DEBUG())
			warnx("release widget '%s'", w1->w_id);

		if (w1->w_plugin->p_delete_widget && w1->w_plugin->p_delete_widget(w1->w_panel) == P_RET_ERR)
			warnx("destructor failed for widget '%s'", w1->w_id);
	}

	w1 = LIST_FIRST(&widgets);
	while (w1) {
		w2 = LIST_NEXT(w1, entries);
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

static void ui_process_tasks(void)
{
	struct ui_task *t = NULL;

	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_process_tasks called not from UI thread");

	pthread_mutex_lock(&ui_mutex);

	t = TAILQ_FIRST(&uitasks);
	TAILQ_INIT(&uitasks);

	pthread_mutex_unlock(&ui_mutex);

	while (t) {
		struct ui_task *next = TAILQ_NEXT(t, entries);
		int rc = 0;

		switch (t->type) {
			case UI_TASK_CREATE:
			case UI_TASK_UPDATE:
			case UI_TASK_DELETE:
			case UI_TASK_FOCUS:
			case UI_TASK_RESULT:
				{
					const char *action = ipc_get_val(req_data(&t->req), "action");
					const char *widget_id = ipc_get_val(req_data(&t->req), "widget");

					if (!action || !widget_id) {
						ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=field is missing",
								req_id(&t->req));
						rc = -1;
						break;
					}

					struct widget *widget = find_widget(widget_id);

					if (streq(action, "create")) {
						if (widget) {
							ipc_send_string(req_fd(&t->req),
									"RESPDATA %s ERR=widget with '%s' already exists",
									req_id(&t->req), widget_id);
							rc = -1;
							break;
						}

						const char *plugin_name = ipc_get_val(req_data(&t->req), "plugin");
						if (!plugin_name) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=field is missing: plugin",
									req_id(&t->req));
							rc = -1;
							break;
						}

						struct plugin *plugin = find_plugin(plugin_name);
						if (!plugin) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=plugin not found",
									req_id(&t->req));
							rc = -1;
							break;
						}

						struct widget *wnew = calloc(1, sizeof(*wnew));
						if (!wnew) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
									req_id(&t->req));
							rc = -1;
							break;
						}

						wnew->w_id = strdup(widget_id);
						wnew->w_plugin = plugin;

						if (!wnew->w_id) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
									req_id(&t->req));
							free(wnew);
							rc = -1;
							break;
						}

						if (plugin->p_create_widget) {
							wnew->w_panel = plugin->p_create_widget(&t->req);
							if (!wnew->w_panel) {
								ipc_send_string(req_fd(&t->req),
										"RESPDATA %s ERR=unable to create widget",
										req_id(&t->req));
								free(wnew->w_id);
								free(wnew);
								rc = -1;
								break;
							}
						}

						LIST_INSERT_HEAD(&widgets, wnew, entries);
						rc = 0;

					} else if (streq(action, "update")) {
						if (!widget) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no widget found by id: %s",
									req_id(&t->req), widget_id);
							rc = -1;
							break;
						}

						if (widget->w_plugin->p_update_widget &&
								widget->w_plugin->p_update_widget(&t->req, widget->w_panel) != P_RET_OK) {
							rc = -1;
							break;
						}

						if (!widget->w_finished && widget->w_plugin->p_finished)
							widget->w_finished = widget->w_plugin->p_finished(widget->w_panel);

						rc = 0;

					} else if (streq(action, "delete")) {
						if (!widget) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no widget found by id: %s",
									req_id(&t->req), widget_id);
							rc = -1;
							break;
						}

						if (widget->w_plugin->p_delete_widget &&
								widget->w_plugin->p_delete_widget(widget->w_panel) != P_RET_OK) {
							rc = -1;
							break;
						}

						LIST_REMOVE(widget, entries);
						free(widget->w_id);
						free(widget);
						rc = 0;

					} else if (streq(action, "focus")) {
						if (!widget) {
							ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no widget found by id: %s",
									req_id(&t->req), widget_id);
							rc = -1;
							break;
						}

						top_panel(widget->w_panel);
						LIST_REMOVE(widget, entries);
						LIST_INSERT_HEAD(&widgets, widget, entries);
						rc = 0;

					} else if (streq(action, "result")) {
						struct ipc_pair *data = req_data(&t->req);

						for (size_t i = 0; i < data->num_kv; i++) {
							if (!streq(data->kv[i].key, "widget"))
								continue;
							struct widget *w = find_widget(data->kv[i].val);
							if (!w) continue;

							ipc_send_string(req_fd(&t->req), "RESPDATA %s WIDGET=%s",
									req_id(&t->req), w->w_id);

							if (w->w_plugin->p_result)
								w->w_plugin->p_result(&t->req, w->w_panel);
						}
						rc = 0;

					} else {
						ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=unknown action",
								req_id(&t->req));
						rc = -1;
					}

					if (use_terminal) {
						update_panels();
						doupdate();
					}

					if (debug_file)
						fflush(stderr);
				}
				break;

			case UI_TASK_SHOW_SPLASH:
				if (!use_terminal) {
					refresh();
					doupdate();
					use_terminal = !use_terminal;
				}
				rc = 0;
				break;

			case UI_TASK_HIDE_SPLASH:
				if (use_terminal) {
					endwin();
					use_terminal = !use_terminal;
				}
				rc = 0;
				break;

			default:
				rc = -1;
				break;
		}

		pthread_mutex_lock(&ui_mutex);
		t->rc = rc;
		done_task_id = t->id;
		pthread_cond_broadcast(&ui_cond);
		pthread_mutex_unlock(&ui_mutex);

		t = next;
	}
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

	const char *action = ipc_get_val(&m->data, "action");
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
		const char *widget_id = ipc_get_val(&m->data, "widget");
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

static void curses_init(void)
{
	initscr();
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
		refresh();
	}
}

static void curses_finish(void)
{
	reset_color_pairs();
	endwin();
}

static void *thread_connection(void *arg)
{
	struct ipc_ctx *ctx = arg;

	ipc_event_loop(ctx);
	ipc_close(ctx);

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

	LIST_INIT(&widgets);
	LIST_INIT(&workers);
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
			wint_t code;
			int ret = get_wch(&code);

			if (ret != ERR) {
				struct widget *focused = LIST_FIRST(&widgets);

				if (focused && focused->w_plugin->p_input) {
					focused->w_plugin->p_input(focused->w_panel, (wchar_t) code);

					if (use_terminal) {
						update_panels();
						doupdate();
					}
				}
			}
		}
		if (pfd[POLL_EVENTFD].revents & POLLIN) {
			uint64_t val;
			while (read(ui_eventfd, &val, sizeof(val)) > 0);

			ui_process_tasks();
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
