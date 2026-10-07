// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/signalfd.h>
#include <sys/ioctl.h>
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
#include <wctype.h>
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
#include "daemon_style.h"
#include "daemon_instance.h"
#include "daemon_task.h"
#include "daemon_worker.h"
#include "daemon_event.h"
#include "daemon_help.h"

static SCREEN *scr = NULL;
static struct daemon_help help;
static FILE *terminal_output;
static bool paste_keys;
static bool paste_enabled;
static bool pasting;

#define KEY_PASTE_BEGIN (KEY_MAX + 1)
#define KEY_PASTE_END (KEY_MAX + 2)

static void set_paste_mode(bool enabled)
{
	enabled = enabled && paste_keys;

	if (paste_enabled == enabled)
		return;

	if (fputs(enabled ? "\033[?2004h" : "\033[?2004l", terminal_output) == EOF ||
	    fflush(terminal_output) == EOF) {
		warn("set terminal paste mode");
		return;
	}

	paste_enabled = enabled;

	if (!enabled)
		pasting = false;
}

static _Atomic int do_quit = 0;
static _Thread_local bool quit_requested;

static bool use_terminal = true;
static char *debug_file = NULL;
static enum widget_theme theme = WIDGET_THEME_AUTO;
static enum daemon_animation animation = DAEMON_ANIMATION_AUTO;
static int animation_duration_ms = 150;

static pthread_t ui_thread;

static const char cmdopts_s[] = "S:Vh";
static const struct option cmdopts[] = {
	{ "debug-file",         required_argument, NULL, 1   },
	{ "tty",                required_argument, NULL, 2   },
	{ "theme",              required_argument, NULL, 3   },
	{ "animation",          required_argument, NULL, 4   },
	{ "animation-duration", required_argument, NULL, 5   },
	{ "socket-file",        required_argument, NULL, 'S' },
	{ "version",            no_argument,       NULL, 'V' },
	{ "help",               no_argument,       NULL, 'h' },
	{ NULL,                 no_argument,       NULL, 0   },
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
	       "   --tty=DEVICE             TTY to use instead of default.\n"
	       "   --debug-file=FILE        File to write debugging information to.\n"
	       "   --socket-file=FILE       Server socket file.\n"
	       "   --theme=NAME             auto (default), basic, or terminal.\n"
	       "   --animation=MODE         auto (default), none, or slide.\n"
	       "   --animation-duration=MS  0..10000 milliseconds (default: 150).\n"
	       "   -V, --version            Show version of program and exit.\n"
	       "   -h, --help               Show this text and exit.\n"
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

static void ui_update_cursor(void)
{
	struct widget *focused = daemon_focus_get();
	int y, x;
	struct instance *focused_ins = NULL;

	if (!focused || !(focused->attrs & ATTR_CAN_CURSOR) || !widget_is_interactive(focused)) {
		curs_set(0);
		setsyx(-1, -1);
		return;
	}

	focused_ins = daemon_instance_find(focused->instance_id);
	if (!focused_ins || focused_ins->finished ||
	    !(focused_ins->root->flags & FLAG_VISIBLE)) {
		curs_set(0);
		setsyx(-1, -1);
		return;
	}

	if (!widget_coordinates_yx(focused, &y, &x) ||
	    (help.panel && x >= getbegx(help.win) &&
	     x < getbegx(help.win) + getmaxx(help.win) && y >= getbegy(help.win) &&
	     y < getbegy(help.win) + getmaxy(help.win))) {
		curs_set(0);
		setsyx(-1, -1);
		return;
	}

	curs_set(1);
	/* Panel updates may leave the virtual screen cursor unspecified. */
	setsyx(y, x);
}

static void ui_update(void)
{
	struct widget *focused = daemon_focus_get();
	static bool terminal_input = false;

	if (!use_terminal)
		return;
	set_paste_mode(focused && focused->type == WIDGET_INPUT);

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

	daemon_help_render(&help, focused);
	update_panels();
	ui_update_cursor();
	doupdate();
}

static struct instance *ui_get_instance_by_id(struct ui_task *t)
{
	const char *instance_id = req_get_val(&t->req, "id");
	struct instance *instance = daemon_instance_find(instance_id);

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
	if (!daemon_instance_create(&t->req))
		return -1;
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

	daemon_focus_validate();
	widget_render_tree(instance->root);

	daemon_instance_check_finished(instance);
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

	daemon_instance_check_finished(instance);
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

	daemon_instance_delete(instance);

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

	if (daemon_instance_focus(instance))
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
		set_paste_mode(false);
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

static struct widget *style_lookup_instance(void *data)
{
	struct instance *instance = ui_get_instance_by_id(data);
	return instance ? instance->root : NULL;
}

static int ui_process_task_set_style(struct ui_task *t)
{
	struct widget *changed;
	if (!daemon_style_apply(&t->req, style_lookup_instance, t, &changed))
		return -1;

	if (!changed)
		widget_style_apply(stdscr, COLOR_PAIR_MAIN);
	struct instance *instance;
	for (instance = daemon_instance_first(); instance; instance = daemon_instance_next(instance)) {
		if (!changed || instance->root == changed ||
		    (instance->root && instance->root->style_owner == changed))
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
	[UI_TASK_NONE]         = { NULL,           ui_process_task_unknown,      false },
	[UI_TASK_DUMP]         = { "dump",         ui_process_task_dump,         true  },
	[UI_TASK_CREATE]       = { "create",       ui_process_task_create,       true  },
	[UI_TASK_UPDATE]       = { "update",       ui_process_task_update,       true  },
	[UI_TASK_SET_VALUE]    = { "set-value",    ui_process_task_set_value,    true  },
	[UI_TASK_DELETE]       = { "delete",       ui_process_task_delete,       true  },
	[UI_TASK_FOCUS]        = { "focus",        ui_process_task_focus,        true  },
	[UI_TASK_RESULT]       = { "result",       ui_process_task_result,       true  },
	[UI_TASK_SHOW_SPLASH]  = { "show-splash",  ui_process_task_show_splash,  false },
	[UI_TASK_HIDE_SPLASH]  = { "hide-splash",  ui_process_task_hide_splash,  false },
	[UI_TASK_SET_TITLE]    = { "set-title",    ui_process_task_set_title,    false },
	[UI_TASK_SET_STYLE]    = { "set-style",    ui_process_task_set_style,    false },
	[UI_TASK_LIST_PLUGINS] = { "list-plugins", ui_process_task_list_plugins, false },
};

static enum ui_task_type find_ui_command(const char *action)
{
	for (enum ui_task_type type = UI_TASK_NONE + 1; type < UI_TASK_COUNT; type++)
		if (streq(ui_commands[type].action, action))
			return type;
	return UI_TASK_NONE;
}

static int ui_dispatch_task(struct ui_task *t)
{
	return ui_commands[t->type].handler(t);
}

static int event_loop_iter(void *data __attribute__((unused)))
{
	/* The IPC loop has sent the quit response before reaching this callback. */
	if (quit_requested) {
		do_quit = 1;
		daemon_task_wakeup();
	}
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
		quit_requested = true;
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
	} else if (streq(action, "wait-event")) {
		if (!req_get_val(&req, "id")) {
			req_error(&req, "field is missing: id");
			return -1;
		}

		return daemon_instance_wait_event(&req) ? 0 : -1;
	} else if (streq(action, "wait-result")) {
		const char *instance_id = req_get_val(&req, "id");
		if (!instance_id) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=field is missing: id", req_id(&req));
			return -1;
		}

		if (!daemon_instance_wait(&req))
			return -1;

		struct ui_task *t = daemon_task_create(UI_TASK_RESULT, &req);
		if (!t) {
			ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no memory", req_id(&req));
			return -1;
		}
		return daemon_task_submit_and_wait(t);
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

	struct ui_task *t = daemon_task_create(ttype, &req);
	if (!t) {
		ipc_send_string(req_fd(&req), "RESPDATA %s ERR=no memory", req_id(&req));
		return -1;
	}

	return daemon_task_submit_and_wait(t);
}

static void ui_resize(void)
{
	struct winsize size;

	if (ioctl(fileno(terminal_output), TIOCGWINSZ, &size) < 0) {
		warn("unable to get terminal size");
		return;
	}

	if (!size.ws_row || !size.ws_col)
		return;

	if (resize_term(size.ws_row, size.ws_col) == ERR) {
		warnx("unable to resize terminal");
		return;
	}

	help.animating = false;
	erase();
	wnoutrefresh(stdscr);
	daemon_instances_resize();
	ui_update();
}

static void handle_resize(int fd)
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

		warnx("unable to read resize signal notification");
		break;
	}

	if (pending)
		ui_resize();
}

static void handle_input(void)
{
	struct widget *focused = daemon_focus_get();
	wint_t code;
	int ret = get_wch(&code);

	if (ret == ERR)
		return;

	/* KEYCODE (F1..F12, arrows, HOME, END, PAGEUP etc, including WINCH) */
	if (ret == KEY_CODE_YES) {
		if (code == KEY_RESIZE) {
			ui_resize();
			return;
		}
	}

	if (!pasting && daemon_help_input(&help, focused, (wchar_t) code, ret == KEY_CODE_YES)) {
		ui_update();
		return;
	}

	if (focused) {
		struct instance *ins = daemon_instance_find(focused->instance_id);
		if (ins && !(ins->root->flags & FLAG_VISIBLE))
			return;
	}

	if (ret == KEY_CODE_YES && code == KEY_PASTE_BEGIN) {
		pasting = paste_enabled;
		return;
	}

	if (ret == KEY_CODE_YES && code == KEY_PASTE_END) {
		pasting = false;
		return;
	}

	if (pasting) {
		if (ret != OK || !focused || focused->type != WIDGET_INPUT)
			return;

		if (code == L'\t' || code == L'\n' || code == L'\r')
			code = L' ';

		if (!iswprint(code))
			return;

		widget_dispatch_input(focused, (wchar_t) code, false);
		ui_update();
		return;
	}

	if ((ret == OK && code == L'\t') || (ret == KEY_CODE_YES && code == KEY_BTAB)) {
		if (ret == KEY_CODE_YES)
			daemon_focus_prev();
		else
			daemon_focus_next();
		if (daemon_focus_get())
			ui_update();
		return;
	}

	if (focused && focused->ops && (focused->ops->input_event || focused->ops->input)) {
		struct instance *instance = daemon_instance_find(focused->instance_id);

		daemon_instance_input(instance, focused, (wchar_t) code, ret == KEY_CODE_YES);
		ui_update();
	}
}

static void handle_tasks(void)
{
	daemon_task_dispatch(ui_dispatch_task);
	daemon_workers_reap();
	if (debug_file)
		fflush(stderr);
}

static void curses_init(FILE *inf, FILE *outf)
{
	scr = newterm(NULL, outf, inf);
	if (!scr)
		errx(EXIT_FAILURE, "newterm failed");

	set_term(scr);

	terminal_output = outf;
	help.animation = daemon_animation_resolve(animation, fileno(outf));
	help.duration_ms = animation_duration_ms;

	paste_keys = define_key("\033[200~", KEY_PASTE_BEGIN) == OK &&
		     define_key("\033[201~", KEY_PASTE_END) == OK;

	if (!paste_keys)
		warnx("unable to define terminal paste keys");

	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	set_escdelay(100);
	curs_set(0);
	if (!widget_style_init(theme))
		errx(EXIT_FAILURE, "unable to initialize terminal theme");
	widget_style_apply(stdscr, COLOR_PAIR_MAIN);

	refresh();
}

static void curses_finish(void)
{
	set_paste_mode(false);
	reset_color_pairs();
	endwin();
	delscreen(scr);
}

int main(int argc, char **argv)
{
	int c, r, retcode;
	const char *tty_file = NULL;
	const char *socket_file = NULL;
	const char *pluginsdir = NULL;

	while ((c = getopt_long(argc, argv, cmdopts_s, cmdopts, NULL)) != -1) {
		switch (c) {
			case 1: // --debug-file=Filename
				debug_file = optarg;
				break;
			case 2: // --tty=TTYDevice
				tty_file = optarg;
				break;
			case 'S': // --socket-file=Filename
				socket_file = optarg;
				break;
			case 3:
				if (streq(optarg, "auto"))
					theme = WIDGET_THEME_AUTO;
				else if (streq(optarg, "basic"))
					theme = WIDGET_THEME_BASIC;
				else if (streq(optarg, "terminal"))
					theme = WIDGET_THEME_TERMINAL;
				else
					errx(EXIT_FAILURE, "unknown theme: %s", optarg);

				break;
			case 4:
				if (streq(optarg, "auto"))
					animation = DAEMON_ANIMATION_AUTO;
				else if (streq(optarg, "none"))
					animation = DAEMON_ANIMATION_NONE;
				else if (streq(optarg, "slide"))
					animation = DAEMON_ANIMATION_SLIDE;
				else
					errx(EXIT_FAILURE, "unknown animation: %s", optarg);

				break;
			case 5: {
				char *end;

				errno = 0;
				long duration = strtol(optarg, &end, 10);

				if (!*optarg || strspn(optarg, "0123456789") != strlen(optarg) ||
				    *end || errno || duration > 10000)
					errx(EXIT_FAILURE, "invalid animation duration: %s", optarg);

				animation_duration_ms = (int) duration;
				break;
			}
			case 'V':
				print_version(basename(argv[0]));
				break;
			case 'h':
				print_help(basename(argv[0]), EXIT_SUCCESS);
				break;
			default:
				print_help(basename(argv[0]), EXIT_FAILURE);
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
	FILE *inf = stdin;
	FILE *tty = NULL;

	if (tty_file && *tty_file) {
		tty = fopen(tty_file, "w+");

		if (!tty)
			err(EXIT_FAILURE, "unable to open terminal device: %s", tty_file);

		inf = outf = tty;
	}

	setlocale(LC_ALL, "");
	setlocale(LC_CTYPE, "");

	/* Every subsequently created thread inherits these blocked signals. */
	sigset_t child_signals, resize_signals, blocked_signals, original_signals;

	sigemptyset(&child_signals);
	sigaddset(&child_signals, SIGCHLD);

	sigemptyset(&resize_signals);
	sigaddset(&resize_signals, SIGWINCH);

	blocked_signals = child_signals;
	sigaddset(&blocked_signals, SIGWINCH);

	r = pthread_sigmask(SIG_BLOCK, &blocked_signals, &original_signals);
	if (r)
		error(EXIT_FAILURE, r, "pthread_sigmask");

	int child_eventfd = signalfd(-1, &child_signals, SFD_CLOEXEC | SFD_NONBLOCK);
	if (child_eventfd < 0)
		err(EXIT_FAILURE, "signalfd");

	int resize_eventfd = signalfd(-1, &resize_signals, SFD_CLOEXEC | SFD_NONBLOCK);
	if (resize_eventfd < 0)
		err(EXIT_FAILURE, "signalfd");

	retcode = EXIT_SUCCESS;

	pluginsdir = getenv("PLAINMOUTH_PLUGINSDIR");

	if (!pluginsdir || !*pluginsdir)
		pluginsdir = PLUGINSDIR;

	load_plugins(pluginsdir);

	daemon_task_init();
	ui_thread = pthread_self();

	struct ipc_ctx ctx;
	ipc_init(&ctx);

	ctx.event_loop_iter = event_loop_iter;
	ctx.handle_message = handle_message;

	curses_init(inf, outf);
	// atexit(curses_finish);

	ipc_listen(&ctx, socket_file, 42, 0);

	enum {
		POLL_SRVFD    = 0,
		POLL_STDIN    = 1,
		POLL_EVENTFD  = 2,
		POLL_CHILDFD  = 3,
		POLL_RESIZEFD = 4,
		POLL_N_FDS    = 5,
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
				.fd = daemon_task_fd(),
				.events = POLLIN,
				},
		[POLL_CHILDFD] = {
				.fd = child_eventfd,
				.events = POLLIN,
				},
		[POLL_RESIZEFD] = {
				.fd = resize_eventfd,
				.events = POLLIN,
				},
	};

	while (!do_quit) {
		struct daemon_events events;
		if (!daemon_events_collect(base_pfd, POLL_N_FDS, &events)) {
			warnx("unable to collect poll descriptors");
			retcode = EXIT_FAILURE;
			break;
		}
		struct pollfd *pfd = events.fds;
		int timeout = widget_select_search_timeout(daemon_focus_get());

		if (timeout == 0) {
			ui_update();
			timeout = -1;
		}

		int help_timeout = daemon_help_timeout(&help);

		if (help_timeout == 0) {
			ui_update();
			help_timeout = daemon_help_timeout(&help);
		}

		if (help_timeout >= 0 && (timeout < 0 || help_timeout < timeout))
			timeout = help_timeout;

		errno = 0;
		r = poll(pfd, (nfds_t) events.count, timeout);

		if (r < 0) {
			int saved_errno = errno;
			daemon_events_free(&events);
			errno = saved_errno;

			if (errno == EINTR)
				continue;

			warn("poll");

			retcode = EXIT_FAILURE;
			break;
		}

		if (r == 0) {
			daemon_events_free(&events);
			ui_update();
			continue;
		}

		/* Dispatch the snapshot before input/tasks can delete its owners. */
		daemon_events_dispatch(&events);

		if (pfd[POLL_CHILDFD].revents & POLLIN)
			daemon_events_handle_children(child_eventfd);

		if (pfd[POLL_RESIZEFD].revents & POLLIN)
			handle_resize(resize_eventfd);

		if (daemon_events_redraw())
			ui_update();

		if (pfd[POLL_SRVFD].revents & POLLIN) {
			struct ipc_ctx *client = ipc_accept(&ctx);

			if (client)
				daemon_worker_start(client);
		}
		if (pfd[POLL_STDIN].revents & POLLIN) {
			handle_input();
		}
		if (pfd[POLL_EVENTFD].revents & POLLIN) {
			handle_tasks();
		}
		daemon_events_free(&events);

		fflush(stderr);
	}

	do_quit = 1;
	daemon_task_stop();
	daemon_instances_stop();
	daemon_workers_stop();

	daemon_help_close(&help);
	daemon_instances_free();
	unload_plugins();
	daemon_styles_free();

	ipc_close(&ctx);
	ipc_free(&ctx);

	daemon_task_free();
	close(child_eventfd);
	close(resize_eventfd);

	r = pthread_sigmask(SIG_SETMASK, &original_signals, NULL);
	if (r)
		error(0, r, "pthread_sigmask");

	curses_finish();

	if (tty && fclose(tty) == EOF) {
		warn("unable to close terminal device: %s", tty_file);
		retcode = EXIT_FAILURE;
	}

	return retcode;
}
