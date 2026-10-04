// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/queue.h>

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
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
	UI_TASK_SET_VALUE,
	UI_TASK_DELETE,
	UI_TASK_FOCUS,
	UI_TASK_RESULT,
	UI_TASK_SHOW_SPLASH,
	UI_TASK_HIDE_SPLASH,
	UI_TASK_SET_TITLE,
	UI_TASK_SET_STYLE,
	UI_TASK_LIST_PLUGINS,
	UI_TASK_COUNT,
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
	bool events_disabled;
	bool redraw_pending;
};
TAILQ_HEAD(instances, instance);

struct named_style {
	TAILQ_ENTRY(named_style)
	entries;
	char *name;
	struct widget *source;
};
TAILQ_HEAD(named_styles, named_style);
static struct named_styles named_styles = TAILQ_HEAD_INITIALIZER(named_styles);

static struct named_style *find_named_style(const char *name)
{
	struct named_style *style;
	TAILQ_FOREACH(style, &named_styles, entries)
	{
		if (streq(style->name, name))
			return style;
	}
	return NULL;
}

static void free_named_style(struct named_style *style)
{
	widget_free(style->source);
	free(style->name);
	free(style);
}

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
		if (instance->plugin && instance->plugin->p_delete_instance &&
		    instance->plugin->p_delete_instance(instance->root) != P_RET_OK) {
			warnx("plugin delete callback failed for instance '%s'", instance->id);
		}

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

static void widget_ensure_visible(struct widget *w)
{
	struct widget *child = w;
	struct widget *cur = w->parent;

	while (cur) {
		if (cur->ops && cur->ops->ensure_visible)
			cur->ops->ensure_visible(cur, child);

		child = cur;
		cur = cur->parent;
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
	static bool terminal_input = false;

	if (!use_terminal)
		return;

	bool needs_raw = focused && focused->type == WIDGET_TERMINAL;
	if (needs_raw != terminal_input) {
		int ret;
		if (needs_raw)
			ret = raw();
		else {
			ret = noraw();
			if (ret != ERR)
				ret = cbreak();
		}
		if (ret == ERR)
			warnx("unable to change terminal input mode");
		else
			terminal_input = needs_raw;
	}

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

	if (state) {
		if (IS_DEBUG())
			warnx("%s (%p) in focus", widget_type(focused), focused->win);

		focused->flags |= FLAG_INFOCUS;

		widget_ensure_visible(focused);

		/*
		 * This is necessary to ensure that the panel with the widget
		 * in focus is on top of everything else.
		 */
		struct instance *ins = find_instance(focused->instance_id);
		widget_render_tree(ins->root);
		top_panel(ins->panel);
	} else {
		if (IS_DEBUG())
			warnx("%s (%p) lost focus", widget_type(focused), focused->win);

		focused->flags &= ~FLAG_INFOCUS;
		widget_render_tree(focused);
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

	const char *style_name = req_get_val(&t->req, "style");
	if (style_name) {
		struct named_style *style = find_named_style(style_name);
		if (!style) {
			req_error(&t->req, "unknown style: %s", style_name);
			return -1;
		}
		t->req.r_style_owner = style->source;
	}

	struct instance *wnew = calloc(1, sizeof(*wnew));
	if (!wnew) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=no memory",
				req_id(&t->req));
		return -1;
	}

	wnew->id = strdup(instance_id);
	if (!wnew->id) {
		req_error(&t->req, "no memory");
		free(wnew);
		return -1;
	}
	wnew->plugin = plugin;

	if (plugin->p_create_instance) {
		wnew->root = plugin->p_create_instance(&t->req);
		if (!wnew->root) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create instance",
					req_id(&t->req));
			free((char *) wnew->id);
			free(wnew);
			return -1;
		}

		wnew->panel = new_panel(wnew->root->win);
		if (!wnew->panel) {
			ipc_send_string(req_fd(&t->req),
					"RESPDATA %s ERR=unable to create panel",
					req_id(&t->req));
			if (wnew->plugin && wnew->plugin->p_delete_instance &&
			    wnew->plugin->p_delete_instance(wnew->root) != P_RET_OK) {
				warnx("plugin delete callback failed for instance '%s'", wnew->id);
			}
			widget_free(wnew->root);
			free((char *) wnew->id);
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

static int ui_process_task_set_value(struct ui_task *t)
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "ui_task_create called not from UI thread");

	struct instance *instance = ui_get_instance_by_id(t);
	if (!instance)
		return -1;

	if (!instance->plugin->p_set_value_instance) {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=set-value is unsupported by plugin",
				req_id(&t->req));
		return -1;
	}

	if (instance->plugin->p_set_value_instance(&t->req, instance->root) != P_RET_OK)
		return -1;

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
	pthread_cond_broadcast(&instance_cond);
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
		char *end;
		errno = 0;
		long value = strtol(color + 5, &end, 10);
		if (errno || *end || value < 0 || value >= COLORS ||
		    color[5] < '0' || color[5] > '9') {
			req_error(req, "invalid color number: %s", color);
			return false;
		}
		num = (int) value;
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

static bool parse_style_attrs(struct request *req, const char *text, attr_t *attrs)
{
	static const struct {
		const char *name;
		attr_t value;
	} names[] = {
		{ "bold",      A_BOLD      },
		{ "dim",       A_DIM       },
		{ "underline", A_UNDERLINE },
		{ "reverse",   A_REVERSE   },
		{ "blink",     A_BLINK     },
		{ "italic",    A_ITALIC    },
	};

	*attrs = A_NORMAL;
	if (streq(text, "normal"))
		return true;

	char *copy = strdup(text);
	if (!copy) {
		req_error(req, "unable to allocate style attributes");
		return false;
	}

	bool valid = true;
	char *remaining = copy, *token;
	while ((token = strsep(&remaining, ","))) {
		bool found = false;
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (streq(token, names[i].name)) {
				*attrs |= names[i].value;
				found = true;
				break;
			}
		}
		if (!found) {
			req_error(req, "unknown style attribute: %s", token);
			valid = false;
			break;
		}
	}
	free(copy);
	return valid;
}

static int ui_process_task_set_style(struct ui_task *t)
{
	const char *name, *fg_name, *bg_name;
	int pair, fg, bg;
	attr_t attrs = A_NORMAL;
	const char *attrs_name = req_get_val(&t->req, "attrs");
	const char *id = req_get_val(&t->req, "id");
	const char *style_name = req_get_val(&t->req, "style");
	bool reset = false;
	if (id && style_name) {
		req_error(&t->req, "id and style cannot be combined");
		return -1;
	}
	if (style_name && !*style_name) {
		req_error(&t->req, "style name cannot be empty");
		return -1;
	}

	name = req_get_val(&t->req, "name");
	fg_name = req_get_val(&t->req, "fg");
	bg_name = req_get_val(&t->req, "bg");

	if (streq(name, "main"))
		pair = COLOR_PAIR_MAIN;
	else if (streq(name, "window"))
		pair = COLOR_PAIR_WINDOW;
	else if (streq(name, "button"))
		pair = COLOR_PAIR_BUTTON;
	else if (streq(name, "focus"))
		pair = COLOR_PAIR_FOCUS;
	else {
		ipc_send_string(req_fd(&t->req), "RESPDATA %s ERR=unknown style: %s",
				req_id(&t->req), name);
		return -1;
	}

	if (attrs_name && !parse_style_attrs(&t->req, attrs_name, &attrs))
		return -1;

	if (!req_read_bool(&t->req, "reset", false, &reset))
		return -1;
	if (req_get_val(&t->req, "reset") && !id && !style_name) {
		req_error(&t->req, "style reset requires an instance id or style name");
		return -1;
	}
	if (reset && (fg_name || bg_name || attrs_name)) {
		req_error(&t->req, "style reset cannot be combined with overrides");
		return -1;
	}
	if (!fg_name && !bg_name && !attrs_name && !reset) {
		req_error(&t->req, "missing style colors or attributes");
		return -1;
	}

	if (id || style_name) {
		struct instance *instance = NULL;
		struct named_style *style = NULL;
		bool new_style = false;
		if (id) {
			instance = ui_get_instance_by_id(t);
			if (!instance)
				return -1;
		} else {
			style = find_named_style(style_name);
			if (!style && reset) {
				req_error(&t->req, "unknown style: %s", style_name);
				return -1;
			}
		}
		if (pair == COLOR_PAIR_MAIN) {
			req_error(&t->req, "main style is global only");
			return -1;
		}
		const int *fg_value = NULL, *bg_value = NULL;
		const attr_t *attrs_value = NULL;
		if (fg_name) {
			if (!convert_color(&t->req, fg_name, &fg))
				return -1;
			fg_value = &fg;
		}
		if (bg_name) {
			if (!convert_color(&t->req, bg_name, &bg))
				return -1;
			bg_value = &bg;
		}
		if (attrs_name)
			attrs_value = &attrs;
		struct widget *target;
		if (instance) {
			target = instance->root;
		} else {
			if (!style) {
				style = calloc(1, sizeof(*style));
				if (!style) {
					req_error(&t->req, "unable to allocate named style");
					return -1;
				}
				style->name = strdup(style_name);
				style->source = widget_create(WIDGET_WINDOW);
				if (!style->name || !style->source) {
					free_named_style(style);
					req_error(&t->req, "unable to allocate named style");
					return -1;
				}
				new_style = true;
			}
			target = style->source;
		}
		if (!widget_style_override(target, pair, fg_value, bg_value, attrs_value, reset)) {
			if (new_style)
				free_named_style(style);
			req_error(&t->req, "unable to update style");
			return -1;
		}
		if (new_style) {
			TAILQ_INSERT_HEAD(&named_styles, style, entries);
		}
		if (instance) {
			widget_render_tree(instance->root);
		} else {
			TAILQ_FOREACH(instance, &instances, entries)
			{
				if (instance->root && instance->root->style_owner == style->source)
					widget_render_tree(instance->root);
			}
		}
		ui_update();
		return 0;
	}

	if (fg_name || bg_name) {
		if (!convert_color(&t->req, fg_name, &fg) ||
		    !convert_color(&t->req, bg_name, &bg))
			return -1;
		if (init_extended_pair(pair, fg, bg) == ERR) {
			req_error(&t->req, "unable to update color pair");
			return -1;
		}
	}
	if (attrs_name)
		widget_style_set_attrs(pair, attrs);

	widget_style_apply(stdscr, COLOR_PAIR_MAIN);
	struct instance *instance;
	TAILQ_FOREACH(instance, &instances, entries)
	{
		widget_render_tree(instance->root);
	}
	ui_update();
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

	FILE *fd = fopen(outfile, "a");
	widget_dump(fd, instance->root);
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

struct ui_command {
	const char *action;
	int (*handler)(struct ui_task *task);
	bool requires_id;
};

static const struct ui_command ui_commands[UI_TASK_COUNT] = {
	[UI_TASK_NONE] = { NULL,           ui_process_task_unknown,      false },
	[UI_TASK_DUMP] = { "dump",         ui_process_task_dump,         true  },
	[UI_TASK_CREATE] = { "create",       ui_process_task_create,       true  },
	[UI_TASK_UPDATE] = { "update",       ui_process_task_update,       true  },
	[UI_TASK_SET_VALUE] = { "set-value",    ui_process_task_set_value,    true  },
	[UI_TASK_DELETE] = { "delete",       ui_process_task_delete,       true  },
	[UI_TASK_FOCUS] = { "focus",        ui_process_task_focus,        true  },
	[UI_TASK_RESULT] = { "result",       ui_process_task_result,       true  },
	[UI_TASK_SHOW_SPLASH] = { "show-splash",  ui_process_task_show_splash,  false },
	[UI_TASK_HIDE_SPLASH] = { "hide-splash",  ui_process_task_hide_splash,  false },
	[UI_TASK_SET_TITLE] = { "set-title",    ui_process_task_set_title,    false },
	[UI_TASK_SET_STYLE] = { "set-style",    ui_process_task_set_style,    false },
	[UI_TASK_LIST_PLUGINS] = { "list-plugins", ui_process_task_list_plugins, false },
};

static enum ui_task_type find_ui_command(const char *action)
{
	for (enum ui_task_type type = UI_TASK_NONE + 1; type < UI_TASK_COUNT; type++)
		if (streq(ui_commands[type].action, action))
			return type;
	return UI_TASK_NONE;
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
		struct ui_task *next = TAILQ_NEXT(t, entries);
		int rc = ui_commands[t->type].handler(t);

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

	enum ui_task_type ttype = find_ui_command(action);
	if (ttype == UI_TASK_NONE) {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=unknown action", req_id(&req));
		return -1;
	}

	if (ui_commands[ttype].requires_id && !req_get_val(&req, "id")) {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: id", req_id(&req));
		return -1;
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

	if (focused && focused->ops && (focused->ops->input_event || focused->ops->input)) {
		struct instance *instance = find_instance(focused->instance_id);

		if (focused->ops->input_event)
			focused->ops->input_event(focused, (wchar_t) code, ret == KEY_CODE_YES);
		else
			focused->ops->input(focused, (wchar_t) code);

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
	TAILQ_FOREACH(ins, &instances, entries)
	{
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
	TAILQ_FOREACH(ins, &instances, entries)
	{
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

	ui_check_instance_finished(ins);
}

static void handle_plugin_events(struct pollfd *fds, struct instance **owners, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		struct instance *ins = owners[i];
		if (!ins || !fds[i].revents || ins->finished || ins->events_disabled)
			continue;
		plugin_event_result(ins, ins->plugin->p_handle_event(ins->root, &fds[i]));
	}
}

static void handle_child_events(int fd)
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

	TAILQ_FOREACH(ins, &instances, entries) {
		if (!ins->finished && !ins->events_disabled && ins->plugin->p_handle_child_event)
			plugin_event_result(ins, ins->plugin->p_handle_child_event(ins->root));
	}
}

static void redraw_plugin_events(void)
{
	bool redraw = false;
	struct instance *ins;
	TAILQ_FOREACH(ins, &instances, entries) {
		if (!ins->redraw_pending)
			continue;

		struct widget *root = ins->root;

		widget_measure_tree(root);
		widget_layout_tree(root, root->lx, root->ly, root->w, root->h);
		widget_render_tree(root);

		ins->redraw_pending = false;
		redraw = true;
	}
	if (redraw)
		ui_update();
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

	/* Every subsequently created thread inherits this blocked signal. */
	sigset_t child_signals, original_signals;
	sigemptyset(&child_signals);
	sigaddset(&child_signals, SIGCHLD);
	r = pthread_sigmask(SIG_BLOCK, &child_signals, &original_signals);
	if (r)
		error(EXIT_FAILURE, r, "pthread_sigmask");
	int child_eventfd = signalfd(-1, &child_signals, SFD_CLOEXEC | SFD_NONBLOCK);
	if (child_eventfd < 0)
		err(EXIT_FAILURE, "signalfd");

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
		POLL_CHILDFD = 3,
		POLL_N_FDS   = 4,
	};

	struct pollfd base_pfd[] = {
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
		[POLL_CHILDFD] = {
			.fd = child_eventfd,
			.events = POLLIN,
		},
	};

	while (!do_quit) {
		struct pollfd *pfd;
		struct instance **owners;
		size_t count;
		if (!collect_pollfds(base_pfd, POLL_N_FDS, &pfd, &owners, &count)) {
			warnx("unable to collect poll descriptors");
			retcode = EXIT_FAILURE;
			break;
		}
		errno = 0;
		r = poll(pfd, (nfds_t) count, -1);

		if (r < 0) {
			int saved_errno = errno;
			free(pfd);
			free(owners);
			errno = saved_errno;
			if (errno == EINTR)
				continue;

			warn("poll");

			retcode = EXIT_FAILURE;
			break;
		}

		if (r == 0) {
			free(pfd);
			free(owners);
			continue;
		}

		/* Dispatch the snapshot before input/tasks can delete its owners. */
		handle_plugin_events(pfd, owners, count);
		if (pfd[POLL_CHILDFD].revents & POLLIN)
			handle_child_events(child_eventfd);
		redraw_plugin_events();

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
		free(pfd);
		free(owners);

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
	while (!TAILQ_EMPTY(&named_styles)) {
		struct named_style *style = TAILQ_FIRST(&named_styles);
		TAILQ_REMOVE(&named_styles, style, entries);
		free_named_style(style);
	}

	ipc_close(&ctx);
	ipc_free(&ctx);

	close(ui_eventfd);
	close(child_eventfd);

	r = pthread_sigmask(SIG_SETMASK, &original_signals, NULL);
	if (r)
		error(0, r, "pthread_sigmask");

	pthread_mutex_destroy(&ui_mutex);
	pthread_cond_destroy(&ui_cond);

	curses_finish();

	return retcode;
}
