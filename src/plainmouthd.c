// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/eventfd.h>
#include <sys/queue.h>

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <getopt.h>
#include <poll.h>
#include <wchar.h>
#include <errno.h>
#include <error.h>
#include <err.h>

#include <pthread.h>
#include <curses.h>

#include "macros.h"
#include "ipc.h"
#include "plugin.h"
#include "request.h"
#include "widget.h"

/*
 * UI task types — what operations need to be performed in the main thread
 */
enum ui_task_type {
	UI_TASK_NONE = 0,
	UI_TASK_DUMP,
	UI_TASK_CREATE,
	UI_TASK_UPDATE,
	UI_TASK_DELETE,
	UI_TASK_FOCUS,
	UI_TASK_RESULT,
	UI_TASK_SHOW_SPLASH,
	UI_TASK_HIDE_SPLASH,
	UI_TASK_SET_TITLE,
	UI_TASK_SET_STYLE,
	UI_TASK_LIST_PLUGINS,
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
	const char *id;
	struct plugin *plugin;
	struct widget *root;
	PANEL *panel;
	bool finished;
};
TAILQ_HEAD(instances, instance);

static struct workers workers;
static struct instances instances;
static struct uitasks uitasks;
static struct widgethead focusable;

static struct widget *focused = NULL;

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
	{ "debug-file",  required_argument, NULL, 1   },
	{ "tty",         required_argument, NULL, 2   },
	{ "socket-file", required_argument, NULL, 'S' },
	{ "version",     no_argument,       NULL, 'V' },
	{ "help",        no_argument,       NULL, 'h' },
	{ NULL,          no_argument,       NULL, 0   },
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
	       "Written by Alexey Gladkov <" PACKAGE_BUGREPORT ">\n"
	       "\n"
	       "Copyright (C) 2025  Alexey Gladkov <" PACKAGE_BUGREPORT ">\n"
	       "This is free software; see the source for copying conditions. There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	       "\n",
	       progname);
	exit(EXIT_SUCCESS);
}

static struct instance *find_instance(const char *id)
{
	if (!id)
		return NULL;

	struct instance *instance;
	TAILQ_FOREACH(instance, &instances, entries) {
		if (streq(instance->id, id))
			return instance;
	}
	return NULL;
}

static void use_instance_widgets(struct instance *ins, struct widget *w)
{
	struct widget *child;

	TAILQ_FOREACH_REVERSE(child, &w->children, widgethead, siblings) {
		use_instance_widgets(ins, child);
	}

	w->instance_id = ins->id;

	if (w->attrs & ATTR_CAN_FOCUS) {
		TAILQ_INSERT_HEAD(&focusable, w, focuses);
	}
}

static void release_instance(struct instance *instance)
{
	if (IS_DEBUG())
		warnx("release instance '%s'", instance->id);

	TAILQ_REMOVE(&instances, instance, entries);

	struct widget *w1 = TAILQ_FIRST(&focusable);
	while (w1) {
		struct widget *w2 = TAILQ_NEXT(w1, focuses);
		if (streq(w1->instance_id, instance->id)) {
			if (w1 == focused)
				focused = NULL;
			TAILQ_REMOVE(&focusable, w1, focuses);
		}
		w1 = w2;
	}

	if (instance->panel) {
		if (IS_DEBUG())
			warnx("destroy panel of instance '%s'", instance->id);
		if (del_panel(instance->panel) == ERR)
			warnx("unable to destroy panel of instance '%s'", instance->id);
		instance->panel = NULL;
	}

	if (instance->root) {
		widget_free(instance->root);
		instance->root = NULL;
	}

	free((char *) instance->id);
	free(instance);
}

static void free_instances(void)
{
	while (!TAILQ_EMPTY(&instances))
		release_instance(TAILQ_FIRST(&instances));
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
	if (w && !w->finished && w->plugin->p_finished) {
		w->finished = w->plugin->p_finished(w->root);

		if (w->finished) {
			pthread_mutex_lock(&instances_mutex);
			pthread_cond_broadcast(&instance_cond);
			pthread_mutex_unlock(&instances_mutex);
		}
	}
}

static void ui_update_cursor(void)
{
	int y, x;
	struct instance *focused_ins = NULL;

	if (!focused || !(focused->attrs & ATTR_CAN_CURSOR)) {
		curs_set(0);
		return;
	}

	focused_ins = find_instance(focused->instance_id);
	if (!focused_ins || focused_ins->finished) {
		curs_set(0);
		return;
	}

	if (!get_abs_cursor(focused_ins->root->win, focused->win, &y, &x)) {
		curs_set(0);
		return;
	}

	curs_set(1);

	wmove(focused_ins->root->win, y, x);
	widget_noutrefresh(focused_ins->root);
}

static void ui_update(void)
{
	if (!use_terminal)
		return;

	if (focused)
		widget_render_tree(focused);

	update_panels();
	ui_update_cursor();
	doupdate();
}

static void ui_focused(bool state)
{
	if (!focused)
		return;

	if (state)
		focused->flags |= FLAG_INFOCUS;
	else
		focused->flags &= ~FLAG_INFOCUS;

	widget_render_tree(focused);

	if (state) {
		/*
		 * This is necessary to ensure that the panel with the widget
		 * in focus is on top of everything else.
		 */
		struct instance *ins = find_instance(focused->instance_id);
		top_panel(ins->panel);
	}
}

static void ui_next_focused(void)
{
	if (focused) {
		ui_focused(false);
		focused = TAILQ_NEXT(focused, focuses);
	}
	if (!focused)
		focused = TAILQ_FIRST(&focusable);
	if (focused) {
		ui_focused(true);
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

	if (plugin->p_create_instance) {
		wnew->root = plugin->p_create_instance(&t->req);
		if (!wnew->root) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create instance",
					req_id(&t->req));
			free(wnew);
			return -1;
		}

		wnew->panel = new_panel(wnew->root->win);
		if (!wnew->panel) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create panel",
					req_id(&t->req));
			// TODO free plugin instance
			free(wnew);
			return -1;
		}
	}

	// A plugin without a callback is always finished.
	wnew->finished = (plugin->p_finished == NULL);

	pthread_mutex_lock(&instances_mutex);

	use_instance_widgets(wnew, wnew->root);
	TAILQ_INSERT_TAIL(&instances, wnew, entries);

	pthread_mutex_unlock(&instances_mutex);

	if (!focused)
		focused = TAILQ_FIRST(&focusable);

	ui_focused(true);
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
			instance->plugin->p_update_instance(&t->req, instance->root) != P_RET_OK) {
		return -1;
	}
	widget_render_tree(instance->root);

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

	pthread_mutex_lock(&instances_mutex);
	release_instance(instance);
	pthread_mutex_unlock(&instances_mutex);

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

	struct widget *w;

	TAILQ_FOREACH(w, &focusable, focuses) {
		if (streq(w->instance_id, instance->id)) {
			focused = w;
			top_panel(instance->panel);
			ui_update();
			break;
		}
	}

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
		instance->plugin->p_result(&t->req, instance->root);

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

static int ui_process_task_set_title(struct ui_task *t)
{
	wchar_t *message __free(ptr) = req_get_wchars(&t->req, "message");

	if (message) {
		wmove(stdscr, 0, 0);
		werase(stdscr);
		w_mvprintw(stdscr, 0, 0, L"%ls", message);
		mvwhline(stdscr, getcury(stdscr) + 1, 0, ACS_HLINE, COLS);
	}

	return 0;
}

static bool convert_color(struct request *req, const char *color, int *cnum)
{
	static const char *builtin_colors[8] = {
		[COLOR_BLACK]   = "black",
		[COLOR_RED]     = "red",
		[COLOR_GREEN]   = "green",
		[COLOR_YELLOW]  = "yellow",
		[COLOR_BLUE]    = "blue",
		[COLOR_MAGENTA] = "magenta",
		[COLOR_CYAN]    = "cyan",
		[COLOR_WHITE]   = "white",
	};
	int num;

	if (!color) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=missing color name",
				req_id(req));
		return false;
	}

	for (num = 0; num < 8; num++)
		if (builtin_colors[num] && streq(color, builtin_colors[num]))
			goto has_number;

	if (streq(color, "default")) {
		num = -1;
		goto has_number;
	}

	if (strlen(color) > 5 && strneq("color", color, 5)) {
		num = atoi(color + 5);
		goto has_number;
	}

	ipc_send_string(req_fd(req), "RESPDATA %s ERR=unknown color name: %s",
			req_id(req), color);
	return false;

has_number:
	if (num >= COLORS) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=color out of range: %s",
				req_id(req), color);
		return false;
	}

	*cnum = num;
	return true;
}

static int ui_process_task_set_style(struct ui_task *t)
{
	const char *name, *fg_name, *bg_name;
	int pair, fg, bg;

	name    = req_get_val(&t->req, "name");
	fg_name = req_get_val(&t->req, "fg");
	bg_name = req_get_val(&t->req, "bg");

	if (streq(name, "main"))        pair = COLOR_PAIR_MAIN;
	else if (streq(name, "window")) pair = COLOR_PAIR_WINDOW;
	else if (streq(name, "button")) pair = COLOR_PAIR_BUTTON;
	else if (streq(name, "focus"))  pair = COLOR_PAIR_FOCUS;
	else {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=unknown style: %s",
				req_id(&t->req), name);
		return -1;
	}

	if (!convert_color(&t->req, fg_name, &fg) ||
	    !convert_color(&t->req, bg_name, &bg))
		return -1;

	if (init_extended_pair(pair, fg, bg) == ERR) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=unable to update color pair",
				req_id(&t->req));
		return -1;
	}

	return 0;
}

static int ui_process_task_dump(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	const char *outfile = req_get_val(&t->req, "filename");
	if (!outfile)
		outfile = "/tmp/plainmouthd.dump";

	int x, y, w, h;
	getmaxyx(instance->root->win, h, w);

	FILE *fd = fopen(outfile, "a");

	fputc('+', fd);
	for (x = 0; x < w; x++)
		fputc('-', fd);
	fputc('+', fd);
	fputc('\n', fd);

	for (y = 0; y < h; y++) {
		fputc('|', fd);

		for (x = 0; x < w; x++) {
			cchar_t cc;

			if (mvwin_wch(instance->root->win, y, x, &cc) == ERR) {
				fputc('?', fd);
				continue;
			}

			attr_t attrs;
			short pair;
			wchar_t wc[CCHARW_MAX + 2];

			if (getcchar(&cc, wc, &attrs, &pair, NULL) == OK && wc[0] != L'\0') {
				if (attrs & A_ALTCHARSET) {
					switch (wc[0]) {
						case 'q': wc[0] = L'─'; break; /* ACS_HLINE */
						case 'x': wc[0] = L'│'; break; /* ACS_VLINE */
						case 'l': wc[0] = L'┌'; break;
						case 'k': wc[0] = L'┐'; break;
						case 'm': wc[0] = L'└'; break;
						case 'j': wc[0] = L'┘'; break;
						default:  wc[0] = L'#'; break;
					}
					wc[1] = L'\0';
				}
			} else {
				wc[0] = L' ';
				wc[1] = L'\0';
			}
			fprintf(fd, "%ls", wc);
		}

		fputc('|', fd);
		fputc('\n', fd);
	}

	fputc('+', fd);
	for (x = 0; x < w; x++)
		fputc('-', fd);
	fputc('+', fd);
	fputc('\n', fd);

	fclose(fd);

	return 0;
}

static int ui_process_task_list_plugins(struct ui_task *t)
{
	int i = 1;

	for (struct plugin *p = list_plugin(NULL); p; p = list_plugin(p)) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s PLUGIN_NAME_%d=%s",
				req_id(&t->req), i, p->name);
		ipc_send_string(req_fd(&t->req), "RESPDATA %s PLUGIN_DESC_%d=%s",
				req_id(&t->req), i, p->desc);
		i++;
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
			case UI_TASK_DUMP:		rc = ui_process_task_dump(t);		break;
			case UI_TASK_CREATE:		rc = ui_process_task_create(t);		break;
			case UI_TASK_UPDATE:		rc = ui_process_task_update(t);		break;
			case UI_TASK_DELETE:		rc = ui_process_task_delete(t);		break;
			case UI_TASK_FOCUS:		rc = ui_process_task_focus(t);		break;
			case UI_TASK_RESULT:		rc = ui_process_task_result(t);		break;
			case UI_TASK_SHOW_SPLASH:	rc = ui_process_task_show_splash(t);	break;
			case UI_TASK_HIDE_SPLASH:	rc = ui_process_task_hide_splash(t);	break;
			case UI_TASK_SET_TITLE:		rc = ui_process_task_set_title(t);	break;
			case UI_TASK_SET_STYLE:		rc = ui_process_task_set_style(t);	break;
			case UI_TASK_LIST_PLUGINS:	rc = ui_process_task_list_plugins(t);	break;
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
	else if (streq(action, "set-title"))	ttype = UI_TASK_SET_TITLE;
	else if (streq(action, "set-style"))	ttype = UI_TASK_SET_STYLE;
	else if (streq(action, "list-plugins"))	ttype = UI_TASK_LIST_PLUGINS;
	else if (streq(action, "dump"))		ttype = UI_TASK_DUMP;
	else {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=unknown action", req_id(&req));
		return -1;
	}

	switch (ttype) {
		case UI_TASK_SHOW_SPLASH:
		case UI_TASK_HIDE_SPLASH:
		case UI_TASK_SET_TITLE:
		case UI_TASK_SET_STYLE:
		case UI_TASK_LIST_PLUGINS:
			break;
		default:
			if (!req_get_val(&req, "id")) {
				ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: id", req_id(&req));
				return -1;
			}
			break;
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

	if (focused && focused->input) {
		struct instance *instance = find_instance(focused->instance_id);

		focused->input(focused, (wchar_t) code);

		ui_check_instance_finished(instance);
		ui_update();
	}
}

static void handle_tasks(void)
{
	uint64_t val;
	while (read(ui_eventfd, &val, sizeof(val)) > 0);

	ui_process_tasks();
}

static void curses_init(FILE *inf, FILE *outf)
{
	scr = newterm(NULL, outf, inf);
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
		init_pair(COLOR_PAIR_FOCUS,  COLOR_WHITE, COLOR_GREEN);
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
	const char *tty_file = NULL;
	const char *socket_file = NULL;
	const char *pluginsdir = NULL;

	while ((c = getopt_long(argc, argv, cmdopts_s, cmdopts, NULL)) != -1) {
		switch (c) {
			case 1:		// --debug-file=Filename
				debug_file = optarg;
				break;
			case 2:		// --tty=TTYDevice
				tty_file = optarg;
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

	FILE *outf = stdout;
	FILE *inf  = stdin;

	if (tty_file && *tty_file) {
		FILE *tty = fopen(tty_file, "w+");
		if (!tty)
			err(EXIT_FAILURE, "unable to open terminal device: %s", tty_file);
		inf = outf = tty;
	}

	setlocale(LC_ALL, "");
	setlocale(LC_CTYPE, "");

	LIST_INIT(&workers);
	TAILQ_INIT(&instances);
	TAILQ_INIT(&uitasks);

	retcode = EXIT_SUCCESS;

	pluginsdir = getenv("PLAINMOUTH_PLUGINSDIR");

	if (!pluginsdir || !*pluginsdir)
		pluginsdir = PLUGINSDIR;

	load_plugins(pluginsdir);

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

	curses_init(inf, outf);
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
			.fd = fileno(inf),
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
