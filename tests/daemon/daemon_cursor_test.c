// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/wait.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vterm.h>

#include "ipc.h"

static pid_t server;
static char socket_path[128];
static char directory[] = "/tmp/plainmouth-cursor-XXXXXX";

static void cleanup(void)
{
	if (server > 0) {
		kill(server, SIGTERM);
		while (waitpid(server, NULL, 0) < 0 && errno == EINTR)
			;
	}
	unlink(socket_path);
	rmdir(directory);
}

static void check(bool condition, int line)
{
	if (!condition) {
		fprintf(stderr, "check failed at line %d\n", line);
		cleanup();
		abort();
	}
}

#define require(condition) check((condition), __LINE__)

static void drain(int fd, VTerm *terminal)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	for (;;) {
		int ret = poll(&pfd, 1, 50);
		if (ret < 0 && errno == EINTR)
			continue;
		require(ret >= 0);
		if (!ret)
			return;
		char buffer[8192];
		ssize_t n = read(fd, buffer, sizeof(buffer));
		require(n > 0);
		require(vterm_input_write(terminal, buffer, (size_t) n) == (size_t) n);
	}
}

static void press(int fd, VTerm *terminal, const char *key)
{
	size_t length = strlen(key);
	require(write(fd, key, length) == (ssize_t) length);
	drain(fd, terminal);
}

static void expect_cursor(VTerm *terminal, int row, int column)
{
	VTermPos position;
	vterm_state_get_cursorpos(vterm_obtain_state(terminal), &position);
	if (position.row != row || position.col != column) {
		fprintf(stderr, "cursor: expected %d,%d, got %d,%d\n", row, column,
			position.row, position.col);
		require(false);
	}
}

static void open_help(int fd, VTerm *terminal)
{
	require(write(fd, "\033OP", 3) == 3);
	VTermScreen *screen = vterm_obtain_screen(terminal);
	struct pollfd pfd = { .fd = fd, .events = POLLIN };

	/* Stop at the first visible frame so the next key arrives during sliding. */
	for (int attempt = 0; attempt < 20; attempt++) {
		int ret = poll(&pfd, 1, 100);

		if (ret < 0 && errno == EINTR)
			continue;

		require(ret >= 0);

		if (!ret)
			continue;

		char buffer[8192];
		ssize_t n = read(fd, buffer, sizeof(buffer));
		require(n > 0);
		require(vterm_input_write(terminal, buffer, (size_t) n) == (size_t) n);

		for (int column = 76; column < 120; column++) {
			VTermScreenCell cell;
			VTermPos position = { .row = 1, .col = column };
			require(vterm_screen_get_cell(screen, position, &cell));

			if (cell.chars[0] && cell.chars[0] != ' ') {
				require(column > 76);
				VTermPos cursor;
				vterm_state_get_cursorpos(vterm_obtain_state(terminal), &cursor);

				if (cursor.row == 11 && cursor.col == 61)
					return;

				break;
			}
		}
	}

	require(false);
}

static void check_compose(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action",  "create"   },
		{ "plugin",  "compose"  },
		{ "id",      "compose"  },
		{ "width",   "30"       },
		{ "height",  "8"        },
		{ "border",  "true"     },
		{ "node",    "vbox"     },
		{ "node",    "hbox"     },
		{ "node",    "label"    },
		{ "text",    "Host: "   },
		{ "node",    "end"      },
		{ "node",    "input"    },
		{ "value",   ""         },
		{ "flex-w",  "1"        },
		{ "node",    "end"      },
		{ "node",    "end"      },
		{ "node",    "password" },
		{ "value",   ""         },
		{ "node",    "end"      },
		{ "node",    "checkbox" },
		{ "node",    "end"      },
		{ "node",    "select"   },
		{ "visible", "2"        },
		{ "option",  "First"    },
		{ "option",  "Second"   },
		{ "node",    "end"      },
		{ "node",    "button"   },
		{ "text",    "OK"       },
		{ "node",    "end"      },
		{ "node",    "end"      },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };

	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "compose"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	drain(master, terminal);
	press(master, terminal, "x\nx\ty\t \t\033OB\n");

	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "result"));
	require(ipc_pair_add(&request, "id", "compose"));
	require(ipc_send_message2(ctx, &request, &response));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	const char *expected[][2] = {
		{ "INPUT_4",    "xx" },
		{ "INPUT_5",    "y"  },
		{ "CHECKBOX_6", "1"  },
		{ "SELECT_7",   "2"  },
		{ "BUTTON_8",   "0"  },
	};
	require(response.num_kv == sizeof(expected) / sizeof(*expected));

	for (size_t i = 0; i < response.num_kv; i++) {
		require(strcmp(response.kv[i].key, expected[i][0]) == 0);
		require(strcmp(response.kv[i].val, expected[i][1]) == 0);
	}

	ipc_pair_free(&response);
	response = (struct ipc_pair) { 0 };
	press(master, terminal, "\t\n");
	require(ipc_pair_add(&request, "action", "wait-result"));
	require(ipc_pair_add(&request, "id", "compose"));
	require(ipc_send_message2(ctx, &request, &response));
	ipc_pair_free(&request);
	require(response.num_kv == 5);
	require(strcmp(response.kv[4].key, "BUTTON_8") == 0);
	require(strcmp(response.kv[4].val, "1") == 0);
	ipc_pair_free(&response);
}

int main(void)
{
	require(mkdtemp(directory) != NULL);
	snprintf(socket_path, sizeof(socket_path), "%s/socket", directory);
	require(setenv("TERM", "xterm", 1) == 0);
	require(setenv("LD_LIBRARY_PATH", ".", 1) == 0);
	require(setenv("PLAINMOUTH_PLUGINSDIR", "plugins", 1) == 0);
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	require(master >= 0);
	require(grantpt(master) == 0 && unlockpt(master) == 0);
	char *slave_name = ptsname(master);
	require(slave_name != NULL);
	int slave = open(slave_name, O_RDWR | O_NOCTTY);
	require(slave >= 0);
	struct winsize size = { .ws_row = 24, .ws_col = 120 };
	require(ioctl(slave, TIOCSWINSZ, &size) == 0);
	server = fork();
	require(server >= 0);
	if (!server) {
		close(master);
		if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 ||
		    dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
		    dup2(slave, STDERR_FILENO) < 0)
			_exit(127);
		close(slave);
		execl("./plainmouthd", "plainmouthd", "-S", socket_path,
		      "--tty=/dev/tty", "--animation-duration=400", (char *) NULL);
		_exit(127);
	}
	close(slave);
	for (int i = 0; access(socket_path, F_OK) != 0; i++) {
		require(i < 200);
		struct timespec pause = { .tv_nsec = 10000000 };
		nanosleep(&pause, NULL);
	}
	struct ipc_ctx ctx;
	ipc_init(&ctx);
	require(ipc_connect(&ctx, socket_path, 0));
	VTerm *terminal = vterm_new(24, 120);
	require(terminal != NULL);
	vterm_set_utf8(terminal, 1);
	VTermScreen *screen = vterm_obtain_screen(terminal);
	vterm_screen_reset(screen, 1);
	const char *fields[][2] = {
		{ "action",   "create"    },
		{ "plugin",   "formbox"   },
		{ "id",       "form"      },
		{ "width",    "30"        },
		{ "height",   "5"         },
		{ "border",   "true"      },
		{ "hbox",     "start"     },
		{ "label",    "Username:" },
		{ "input",    "legion"    },
		{ "hbox",     "end"       },
		{ "hbox",     "start"     },
		{ "label",    "Password:" },
		{ "password", ""          },
		{ "hbox",     "end"       },
		{ "button",   "OK"        },
		{ "button",   "Cancel"    },
	};
	struct ipc_pair request = { 0 };
	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));
	require(ipc_send_message2(&ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	drain(master, terminal);
	press(master, terminal, "\t");
	expect_cursor(terminal, 11, 61);
	open_help(master, terminal);
	expect_cursor(terminal, 11, 61);
	press(master, terminal, "x");
	expect_cursor(terminal, 11, 62);
	press(master, terminal, "\177");
	expect_cursor(terminal, 11, 61);
	press(master, terminal, "\033OD");
	expect_cursor(terminal, 11, 60);
	press(master, terminal, "\033OC");
	expect_cursor(terminal, 11, 61);
	for (int i = 0; i < 14; i++) {
		press(master, terminal, "x");
		int column = 62 + i;
		if (column > 73)
			column = 73;
		expect_cursor(terminal, 11, column);
	}
	press(master, terminal, "\033OP");
	expect_cursor(terminal, 11, 73);
	check_compose(&ctx, master, terminal);
	require(ipc_pair_add(&request, "action", "quit"));
	require(ipc_send_message2(&ctx, &request, NULL));
	ipc_pair_free(&request);
	ipc_close(&ctx);
	ipc_free(&ctx);
	int status;
	require(waitpid(server, &status, 0) == server);
	server = 0;
	require(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	close(master);
	vterm_free(terminal);
	cleanup();
	return 0;
}
