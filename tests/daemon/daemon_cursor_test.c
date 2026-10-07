// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/wait.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
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

static int64_t monotonic_ms(void)
{
	struct timespec now;
	require(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
	return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool read_terminal(int fd, VTerm *terminal, int timeout)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret;

	do {
		ret = poll(&pfd, 1, timeout);
	} while (ret < 0 && errno == EINTR);

	require(ret >= 0);

	if (!ret)
		return false;

	char buffer[8192];
	ssize_t n = read(fd, buffer, sizeof(buffer));
	require(n > 0);
	require(vterm_input_write(terminal, buffer, (size_t) n) == (size_t) n);
	return true;
}

static void drain(int fd, VTerm *terminal)
{
	while (read_terminal(fd, terminal, 50))
		;
}

static bool await_output(int fd, VTerm *terminal, int64_t deadline)
{
	int64_t remaining = deadline - monotonic_ms();

	if (remaining <= 0)
		return false;

	return read_terminal(fd, terminal, (int) remaining);
}

static void dump_screen(VTerm *terminal)
{
	int rows, cols;
	vterm_get_size(terminal, &rows, &cols);

	for (int row = 0; row < rows; row++) {
		fputc('|', stderr);

		for (int col = 0; col < cols; col++) {
			VTermScreenCell cell;
			VTermPos position = { .row = row, .col = col };
			require(vterm_screen_get_cell(vterm_obtain_screen(terminal), position, &cell));
			uint32_t character = cell.chars[0];
			fputc(character >= 32 && character < 127 ? (int) character : ' ', stderr);
		}

		fputs("|\n", stderr);
	}
}

static void press(int fd, VTerm *terminal, const char *key)
{
	size_t length = strlen(key);
	require(write(fd, key, length) == (ssize_t) length);
	drain(fd, terminal);
}

static void expect_cursor(int fd, VTerm *terminal, int row, int column)
{
	int64_t deadline = monotonic_ms() + 2000;
	VTermPos position;

	for (;;) {
		vterm_state_get_cursorpos(vterm_obtain_state(terminal), &position);

		if (position.row == row && position.col == column)
			return;

		if (!await_output(fd, terminal, deadline))
			break;
	}

	fprintf(stderr, "cursor: expected %d,%d, got %d,%d\n", row, column,
		position.row, position.col);
	dump_screen(terminal);
	require(false);
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

static void check_compose_events(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action", "create" },
		{ "plugin", "compose" },
		{ "id", "events" },
		{ "width", "32" },
		{ "height", "6" },
		{ "node", "vbox" },
		{ "node", "button" },
		{ "text", "Test" },
		{ "node-id", "test" },
		{ "close", "false" },
		{ "node", "end" },
		{ "node", "button" },
		{ "text", "OK" },
		{ "node", "end" },
		{ "node", "end" },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "events"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	drain(master, terminal);

	for (int i = 0; i < 2; i++) {
		press(master, terminal, "\n");
		request = (struct ipc_pair) { 0 };
		struct ipc_pair response = { 0 };
		require(ipc_pair_add(&request, "action", "wait-event"));
		require(ipc_pair_add(&request, "id", "events"));
		require(ipc_send_message2(ctx, &request, &response));
		require(response.num_kv == 3);
		require(strcmp(response.kv[0].key, "EVENT") == 0);
		require(strcmp(response.kv[0].val, "button") == 0);
		require(strcmp(response.kv[1].key, "NODE") == 0);
		require(strcmp(response.kv[1].val, "2") == 0);
		require(strcmp(response.kv[2].key, "NODE_ID") == 0);
		require(strcmp(response.kv[2].val, "test") == 0);
		ipc_pair_free(&request);
		ipc_pair_free(&response);
	}

	press(master, terminal, "\t\n");
	request = (struct ipc_pair) { 0 };
	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "wait-result"));
	require(ipc_pair_add(&request, "id", "events"));
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 2);
	require(strcmp(response.kv[0].val, "0") == 0);
	require(strcmp(response.kv[1].val, "1") == 0);
	ipc_pair_free(&request);
	ipc_pair_free(&response);
}

static void set_compose_state(struct ipc_ctx *ctx, int node, const char *key, const char *value)
{
	struct ipc_pair request = { 0 };
	require(ipc_pair_add(&request, "action", "update"));
	require(ipc_pair_add(&request, "id", "states"));
	require(ipc_pair_sprintf(&request, "node", "%d", node));
	require(ipc_pair_add(&request, key, value));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
}

static void check_compose_states(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action",   "create"  },
		{ "plugin",   "compose" },
		{ "id",       "states"  },
		{ "width",    "32"      },
		{ "height",   "6"       },
		{ "node",     "vbox"    },
		{ "node",     "hbox"    },
		{ "node",     "input"   },
		{ "value",    ""        },
		{ "disabled", "true"    },
		{ "node",     "end"     },
		{ "node",     "input"   },
		{ "value",    ""        },
		{ "readonly", "true"    },
		{ "node",     "end"     },
		{ "node",     "input"   },
		{ "value",    ""        },
		{ "node",     "end"     },
		{ "node",     "end"     },
		{ "node",     "button"  },
		{ "text",     "OK"      },
		{ "node",     "end"     },
		{ "node",     "end"     },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "states"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	drain(master, terminal);
	press(master, terminal, "a\t\033[Zb");
	set_compose_state(ctx, 2, "disabled", "true");
	press(master, terminal, "x");
	set_compose_state(ctx, 2, "disabled", "false");
	press(master, terminal, "\033[Zc");
	set_compose_state(ctx, 3, "disabled", "false");
	press(master, terminal, "\033[Zd");
	set_compose_state(ctx, 3, "readonly", "true");
	press(master, terminal, "e");
	set_compose_state(ctx, 4, "readonly", "false");
	press(master, terminal, "\033[Zf");
	request = (struct ipc_pair) { 0 };
	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "result"));
	require(ipc_pair_add(&request, "id", "states"));
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 4);
	require(strcmp(response.kv[0].val, "d") == 0);
	require(strcmp(response.kv[1].val, "f") == 0);
	require(strcmp(response.kv[2].val, "abce") == 0);
	require(strcmp(response.kv[3].val, "0") == 0);
	ipc_pair_free(&request);
	ipc_pair_free(&response);
}

static bool screen_contains(VTerm *terminal, const char *text)
{
	VTermScreen *screen = vterm_obtain_screen(terminal);
	int rows, cols;
	vterm_get_size(terminal, &rows, &cols);
	size_t length = strlen(text);

	for (int row = 0; row < rows; row++) {
		for (int col = 0; col + (int) length <= cols; col++) {
			size_t i;

			for (i = 0; i < length; i++) {
				VTermScreenCell cell;
				VTermPos position = { .row = row, .col = col + (int) i };
				require(vterm_screen_get_cell(screen, position, &cell));

				if (cell.chars[0] != (unsigned char) text[i])
					break;
			}

			if (i == length)
				return true;
		}
	}

	return false;
}

static void expect_screen_text(int fd, VTerm *terminal, const char *text, bool present)
{
	int64_t deadline = monotonic_ms() + 2000;

	while (screen_contains(terminal, text) != present) {
		if (!await_output(fd, terminal, deadline)) {
			fprintf(stderr, "screen: expected '%s' to be %s\n", text,
				present ? "present" : "absent");
			dump_screen(terminal);
			require(false);
		}
	}
}

static void expect_screen_character(int fd, VTerm *terminal, int row, int col, uint32_t character)
{
	VTermScreenCell cell;
	VTermPos position = { .row = row, .col = col };
	int64_t deadline = monotonic_ms() + 2000;

	for (;;) {
		require(vterm_screen_get_cell(vterm_obtain_screen(terminal), position, &cell));

		if (cell.chars[0] == character)
			return;

		if (!await_output(fd, terminal, deadline))
			break;
	}

	fprintf(stderr, "screen at %d,%d: expected U+%04X, got U+%04X\n",
		row, col, character, cell.chars[0]);
	dump_screen(terminal);
	require(false);
}

static void check_compose_scroll(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	struct ipc_pair request = { 0 };
	const char *old_ids[] = { "form", "compose" };

	for (size_t i = 0; i < sizeof(old_ids) / sizeof(*old_ids); i++) {
		require(ipc_pair_add(&request, "action", "delete"));
		require(ipc_pair_add(&request, "id", old_ids[i]));
		require(ipc_send_message2(ctx, &request, NULL));
		ipc_pair_free(&request);
		request = (struct ipc_pair) { 0 };
	}

	const char *fields[][2] = {
		{ "action", "create"  },
		{ "plugin", "compose" },
		{ "id",     "scroll"  },
		{ "x",      "0"       },
		{ "y",      "0"       },
		{ "width",  "30"      },
		{ "height", "8"       },
		{ "border", "true"    },
		{ "node",   "vbox"    },
		{ "node",   "scroll"  },
		{ "flex-h", "1"       },
		{ "node",   "vbox"    },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	for (int i = 1; i <= 10; i++) {
		require(ipc_pair_add(&request, "node", "hbox"));
		require(ipc_pair_add(&request, "node", "label"));
		require(ipc_pair_sprintf(&request, "text", "Field %d: ", i));
		require(ipc_pair_add(&request, "node", "end"));
		require(ipc_pair_add(&request, "node", "input"));
		require(ipc_pair_add(&request, "value", ""));
		require(ipc_pair_add(&request, "flex-w", "1"));
		require(ipc_pair_add(&request, "node", "end"));
		require(ipc_pair_add(&request, "node", "end"));
	}

	require(ipc_pair_add(&request, "node", "end"));
	require(ipc_pair_add(&request, "node", "end"));
	require(ipc_pair_add(&request, "node", "button"));
	require(ipc_pair_add(&request, "text", "OK"));
	require(ipc_pair_add(&request, "node", "end"));
	require(ipc_pair_add(&request, "node", "end"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	drain(master, terminal);
	expect_screen_text(master, terminal, "Field 1:", true);
	expect_screen_text(master, terminal, "Field 10:", false);
	expect_screen_character(master, terminal, 1, 28, '^');
	press(master, terminal, "\t\033OB\033OB");
	expect_screen_text(master, terminal, "Field 3:", true);
	expect_screen_text(master, terminal, "Field 1:", false);
	expect_screen_text(master, terminal, "[OK]", true);
	expect_screen_character(master, terminal, 2, 28, '^');
	expect_screen_character(master, terminal, 3, 28, 'v');
	press(master, terminal, "\033OA\033OA");
	expect_screen_text(master, terminal, "Field 1:", true);
	expect_screen_character(master, terminal, 1, 28, '^');
	press(master, terminal, "\t\t\t\t\t\t\t\t\tz");
	expect_screen_text(master, terminal, "Field 10: z", true);
	expect_screen_text(master, terminal, "Field 1:", false);
	expect_screen_text(master, terminal, "[OK]", true);
	expect_cursor(master, terminal, 5, 12);

	for (int i = 0; i < 3; i++) {
		struct winsize size = { .ws_row = 6, .ws_col = 20 };
		vterm_set_size(terminal, 6, 20);
		require(ioctl(master, TIOCSWINSZ, &size) == 0);
		drain(master, terminal);
		expect_screen_text(master, terminal, "Field 10: z", true);
		expect_screen_text(master, terminal, "[OK]", true);
		expect_cursor(master, terminal, 3, 12);
		size = (struct winsize) { .ws_row = 24, .ws_col = 120 };
		vterm_set_size(terminal, 24, 120);
		require(ioctl(master, TIOCSWINSZ, &size) == 0);
		drain(master, terminal);
		expect_screen_text(master, terminal, "Field 10: z", true);
		expect_screen_text(master, terminal, "[OK]", true);
		expect_cursor(master, terminal, 5, 12);
	}

	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "result"));
	require(ipc_pair_add(&request, "id", "scroll"));
	require(ipc_send_message2(ctx, &request, &response));
	ipc_pair_free(&request);
	require(response.num_kv == 11);
	require(strcmp(response.kv[9].key, "INPUT_33") == 0);
	require(strcmp(response.kv[9].val, "z") == 0);
	require(strcmp(response.kv[10].key, "BUTTON_34") == 0);
	require(strcmp(response.kv[10].val, "0") == 0);
	ipc_pair_free(&response);
	press(master, terminal, "\033[Z\033[Z\033[Z\033[Z\033[Z\033[Z\033[Z\033[Z\033[Z");
	expect_screen_text(master, terminal, "Field 1:", true);
	expect_screen_text(master, terminal, "Field 10:", false);
}

static void check_compose_layout(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action", "create"  },
		{ "plugin", "compose" },
		{ "id",     "layout"  },
		{ "width",  "32"      },
		{ "height", "12"      },
		{ "border", "true"    },
		{ "x",      "0"       },
		{ "y",      "0"       },
		{ "node",   "vbox"    },
		{ "gap",    "1"       },
		{ "node",   "label"   },
		{ "text",   "Layout"  },
		{ "node",   "end"     },
		{ "node",   "hbox"    },
		{ "gap",    "2"       },
		{ "node",   "label"   },
		{ "text",   "Host:"   },
		{ "node",   "end"     },
		{ "node",   "input"   },
		{ "value",  ""        },
		{ "flex-w", "1"       },
		{ "node",   "end"     },
		{ "node",   "end"     },
		{ "node",   "spacer"  },
		{ "height", "1"       },
		{ "flex-h", "1"       },
		{ "node",   "end"     },
		{ "node",   "hbox"    },
		{ "gap",    "2"       },
		{ "node",   "spacer"  },
		{ "flex-w", "1"       },
		{ "node",   "end"     },
		{ "node",   "button"  },
		{ "text",   "OK"      },
		{ "node",   "end"     },
		{ "node",   "button"  },
		{ "text",   "Cancel"  },
		{ "node",   "end"     },
		{ "node",   "end"     },
		{ "node",   "end"     },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "layout"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	drain(master, terminal);
	press(master, terminal, "x");
	expect_cursor(master, terminal, 3, 9);
	expect_screen_character(master, terminal, 10, 17, '[');
	expect_screen_character(master, terminal, 10, 21, ' ');
	expect_screen_character(master, terminal, 10, 22, ' ');
	expect_screen_character(master, terminal, 10, 23, '[');
	expect_screen_character(master, terminal, 10, 30, ']');
	struct winsize size = { .ws_row = 10, .ws_col = 24 };
	vterm_set_size(terminal, 10, 24);
	require(ioctl(master, TIOCSWINSZ, &size) == 0);
	drain(master, terminal);
	expect_cursor(master, terminal, 3, 9);
	expect_screen_character(master, terminal, 8, 9, '[');
	expect_screen_character(master, terminal, 8, 13, ' ');
	expect_screen_character(master, terminal, 8, 14, ' ');
	expect_screen_character(master, terminal, 8, 15, '[');
	expect_screen_character(master, terminal, 8, 22, ']');
	size = (struct winsize) { .ws_row = 24, .ws_col = 120 };
	vterm_set_size(terminal, 24, 120);
	require(ioctl(master, TIOCSWINSZ, &size) == 0);
	drain(master, terminal);
	expect_screen_character(master, terminal, 10, 17, '[');
	expect_screen_character(master, terminal, 10, 30, ']');
	press(master, terminal, "\t\n");
	request = (struct ipc_pair) { 0 };
	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "wait-result"));
	require(ipc_pair_add(&request, "id", "layout"));
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 3);
	require(strcmp(response.kv[0].key, "INPUT_5") == 0);
	require(strcmp(response.kv[0].val, "x") == 0);
	require(strcmp(response.kv[1].key, "BUTTON_9") == 0);
	require(strcmp(response.kv[1].val, "1") == 0);
	require(strcmp(response.kv[2].key, "BUTTON_10") == 0);
	require(strcmp(response.kv[2].val, "0") == 0);
	ipc_pair_free(&request);
	ipc_pair_free(&response);
}

static void check_compose_spinbox(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action", "create"    },
		{ "plugin", "compose"   },
		{ "id",     "numbers"   },
		{ "width",  "30"        },
		{ "height", "8"         },
		{ "border", "true"      },
		{ "x",      "0"         },
		{ "y",      "0"         },
		{ "node",   "vbox"      },
		{ "node",   "hbox"      },
		{ "node",   "label"     },
		{ "text",   "Retries: " },
		{ "node",   "end"       },
		{ "node",   "spinbox"   },
		{ "min",    "0"         },
		{ "max",    "10"        },
		{ "step",   "2"         },
		{ "value",  "3"         },
		{ "node",   "end"       },
		{ "node",   "end"       },
		{ "node",   "scroll"    },
		{ "flex-h", "1"         },
		{ "node",   "vbox"      },
		{ "node",   "label"     },
		{ "text",   "Top"       },
		{ "node",   "end"       },
		{ "node",   "spacer"    },
		{ "height", "5"         },
		{ "node",   "end"       },
		{ "node",   "hbox"      },
		{ "node",   "label"     },
		{ "text",   "Offset: "  },
		{ "node",   "end"       },
		{ "node",   "spinbox"   },
		{ "min",    "-100"      },
		{ "max",    "100"       },
		{ "step",   "5"         },
		{ "value",  "-10"       },
		{ "node",   "end"       },
		{ "node",   "end"       },
		{ "node",   "end"       },
		{ "node",   "end"       },
		{ "node",   "button"    },
		{ "text",   "OK"        },
		{ "node",   "end"       },
		{ "node",   "end"       },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "numbers"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	drain(master, terminal);
	expect_screen_text(master, terminal, "Retries: [03]", true);
	expect_screen_text(master, terminal, "Offset:", false);
	press(master, terminal, "\033OA");
	expect_screen_text(master, terminal, "Retries: [05]", true);
	press(master, terminal, "\033OB");
	expect_screen_text(master, terminal, "Retries: [03]", true);
	press(master, terminal, "7");
	press(master, terminal, "\n");
	expect_screen_text(master, terminal, "Retries: [07]", true);
	press(master, terminal, "\t");
	press(master, terminal, "\t");
	expect_screen_text(master, terminal, "Offset: [-010]", true);
	press(master, terminal, "-");
	press(master, terminal, "2");
	press(master, terminal, "5");
	press(master, terminal, "\n");
	expect_screen_text(master, terminal, "Offset: [-025]", true);
	press(master, terminal, "\033OA");
	expect_screen_text(master, terminal, "Offset: [-020]", true);
	press(master, terminal, "\t");
	press(master, terminal, "\n");
	request = (struct ipc_pair) { 0 };
	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "wait-result"));
	require(ipc_pair_add(&request, "id", "numbers"));
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 3);
	require(strcmp(response.kv[0].key, "SPINBOX_4") == 0);
	require(strcmp(response.kv[0].val, "7") == 0);
	require(strcmp(response.kv[1].key, "SPINBOX_11") == 0);
	require(strcmp(response.kv[1].val, "-20") == 0);
	require(strcmp(response.kv[2].key, "BUTTON_12") == 0);
	require(strcmp(response.kv[2].val, "1") == 0);
	ipc_pair_free(&request);
	ipc_pair_free(&response);
}

static void check_compose_textview(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	struct ipc_pair request = { 0 };
	const char text[] = "First line\nSecond line\nThird line\nFourth line\nFifth line\nLast line";
	const char *fields[][2] = {
		{ "action",  "create"   },
		{ "plugin",  "compose"  },
		{ "id",      "text"     },
		{ "width",   "32"       },
		{ "height",  "7"        },
		{ "x",       "0"        },
		{ "y",       "0"        },
		{ "border",  "true"     },
		{ "node",    "vbox"     },
		{ "node",    "textview" },
		{ "node-id", "output"   },
		{ "flex-h",  "1"        },
		{ "text",    text       },
		{ "node",    "end"      },
		{ "node",    "button"   },
		{ "text",    "OK"       },
		{ "node",    "end"      },
		{ "node",    "end"      },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "text"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	expect_screen_text(master, terminal, "First line", true);
	expect_screen_text(master, terminal, "Last line", false);
	press(master, terminal, "\033OP");
	expect_screen_text(master, terminal, "Scroll one page down", true);
	press(master, terminal, "\033OP");
	expect_screen_text(master, terminal, "Scroll one page down", false);
	press(master, terminal, "\033OF");
	expect_screen_text(master, terminal, "Last line", true);
	expect_screen_text(master, terminal, "First line", false);
	press(master, terminal, "\033OH");
	expect_screen_text(master, terminal, "First line", true);
	press(master, terminal, "\033OB");
	expect_screen_text(master, terminal, "First line", false);
	press(master, terminal, "\033OA");
	expect_screen_text(master, terminal, "First line", true);
	press(master, terminal, "\033OF");
	expect_screen_text(master, terminal, "Last line", true);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "set-value"));
	require(ipc_pair_add(&request, "id", "text"));
	require(ipc_pair_add(&request, "node-id", "output"));
	require(ipc_pair_add(&request, "value", "Replacement"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	expect_screen_text(master, terminal, "Replacement", true);
	expect_screen_text(master, terminal, "Last line", false);
	press(master, terminal, "\t");
	press(master, terminal, "\n");
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "wait-result"));
	require(ipc_pair_add(&request, "id", "text"));
	struct ipc_pair response = { 0 };
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 1 && strcmp(response.kv[0].key, "BUTTON_3") == 0 &&
		strcmp(response.kv[0].val, "1") == 0);
	ipc_pair_free(&response);
	ipc_pair_free(&request);
}

static void expect_node_value(struct ipc_ctx *ctx, const char *node, const char *expected)
{
	struct ipc_pair request = { 0 }, response = { 0 };
	require(ipc_pair_add(&request, "action", "get-value"));
	require(ipc_pair_add(&request, "id", "changes"));
	require(ipc_pair_add(&request, "node-id", node));
	require(ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 1);
	require(strcmp(response.kv[0].key, "VALUE") == 0);
	require(strcmp(response.kv[0].val, expected) == 0);
	ipc_pair_free(&request);
	ipc_pair_free(&response);
}

static void check_compose_changes(struct ipc_ctx *ctx, int master, VTerm *terminal)
{
	const char *fields[][2] = {
		{ "action",     "create"   },
		{ "plugin",     "compose"  },
		{ "id",         "changes"  },
		{ "width",      "32"       },
		{ "height",     "10"       },
		{ "x",          "0"        },
		{ "y",          "0"        },
		{ "node",       "vbox"     },
		{ "node",       "checkbox" },
		{ "node-id",    "tls"      },
		{ "notify",     "true"     },
		{ "node",       "end"      },
		{ "node",       "select"   },
		{ "notify",     "true"     },
		{ "option",     "One"      },
		{ "option",     "Two"      },
		{ "node",       "end"      },
		{ "node",       "spinbox"  },
		{ "node-id",    "count"    },
		{ "notify",     "true"     },
		{ "min",        "-10"      },
		{ "max",        "10"       },
		{ "value",      "0"        },
		{ "node",       "end"      },
		{ "node",       "checkbox" },
		{ "notify",     "false"    },
		{ "node",       "end"      },
		{ "node",       "input"    },
		{ "node-id",    "host"     },
		{ "value",      "ab"       },
		{ "max-length", "3"        },
		{ "notify",     "true"     },
		{ "node",       "end"      },
		{ "node",       "password" },
		{ "node-id",    "secret"   },
		{ "value",      "pw"       },
		{ "notify",     "true"     },
		{ "node",       "end"      },
		{ "node",       "button"   },
		{ "text",       "Test"     },
		{ "close",      "false"    },
		{ "node",       "end"      },
		{ "node",       "button"   },
		{ "text",       "OK"       },
		{ "node",       "end"      },
		{ "node",       "end"      },
	};
	struct ipc_pair request = { 0 };

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		require(ipc_pair_add(&request, fields[i][0], fields[i][1]));

	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "focus"));
	require(ipc_pair_add(&request, "id", "changes"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	press(master, terminal, " ");
	press(master, terminal, " ");
	press(master, terminal, "\t");
	press(master, terminal, "\033OB");
	press(master, terminal, "\033OB");
	press(master, terminal, "\t");
	press(master, terminal, "\033OA");
	press(master, terminal, "2");
	expect_node_value(ctx, "count", "1");
	press(master, terminal, "\n");
	expect_node_value(ctx, "count", "2");
	request = (struct ipc_pair) { 0 };
	require(ipc_pair_add(&request, "action", "set-value"));
	require(ipc_pair_add(&request, "id", "changes"));
	require(ipc_pair_add(&request, "node-id", "count"));
	require(ipc_pair_add(&request, "value", "5"));
	require(ipc_send_message2(ctx, &request, NULL));
	ipc_pair_free(&request);
	press(master, terminal, "\033OA");
	press(master, terminal, "\t");
	press(master, terminal, " ");
	press(master, terminal, "\t");
	press(master, terminal, "c");
	press(master, terminal, "d");
	expect_node_value(ctx, "host", "abc");
	press(master, terminal, "\033OH");
	press(master, terminal, "\033[3~");
	press(master, terminal, "x");
	press(master, terminal, "\033OF");
	press(master, terminal, "\177");
	expect_node_value(ctx, "host", "xb");
	press(master, terminal, "\t");
	press(master, terminal, "\033OH");
	press(master, terminal, "\033[3~");
	press(master, terminal, "z");
	press(master, terminal, "\033OF");
	press(master, terminal, "\177");
	expect_node_value(ctx, "secret", "z");
	press(master, terminal, "\t");
	press(master, terminal, "\n");

	for (int i = 0; i < 6; i++)
		press(master, terminal, "\033[Z");

	press(master, terminal, " ");

	for (int i = 0; i < 6; i++)
		press(master, terminal, "\t");

	press(master, terminal, "\n");
	press(master, terminal, "\t");
	press(master, terminal, "\n");
	const char *nodes[] = { "2", "3", "4", "6", "7", "8", "2", "8" };
	const char *names[] = { "tls", NULL, "count", "host", "secret", NULL, "tls", NULL };
	expect_node_value(ctx, "count", "6");
	expect_node_value(ctx, "tls", "1");

	for (size_t i = 0; i < sizeof(nodes) / sizeof(*nodes); i++) {
		request = (struct ipc_pair) { 0 };
		struct ipc_pair response = { 0 };
		require(ipc_pair_add(&request, "action", "wait-event"));
		require(ipc_pair_add(&request, "id", "changes"));
		require(ipc_send_message2(ctx, &request, &response));
		size_t count = 2;
		require(strcmp(response.kv[0].key, "EVENT") == 0);
		require(strcmp(response.kv[0].val, i == 5 || i == 7 ? "button" : "change") == 0);
		require(strcmp(response.kv[1].key, "NODE") == 0);
		require(strcmp(response.kv[1].val, nodes[i]) == 0);

		if (names[i]) {
			require(strcmp(response.kv[count].key, "NODE_ID") == 0);
			require(strcmp(response.kv[count++].val, names[i]) == 0);
		}

		require(response.num_kv == count);
		ipc_pair_free(&request);
		ipc_pair_free(&response);
	}

	request = (struct ipc_pair) { 0 };
	struct ipc_pair response = { 0 };
	require(ipc_pair_add(&request, "action", "wait-event"));
	require(ipc_pair_add(&request, "id", "changes"));
	require(!ipc_send_message2(ctx, &request, &response));
	require(response.num_kv == 1 && strcmp(response.kv[0].val, "instance finished") == 0);
	ipc_pair_free(&request);
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

		/* Keep diagnostics in the test log, outside the terminal stream. */
		if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 ||
		    dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0)
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
	expect_cursor(master, terminal, 11, 61);
	open_help(master, terminal);
	expect_cursor(master, terminal, 11, 61);
	press(master, terminal, "x");
	expect_cursor(master, terminal, 11, 62);
	press(master, terminal, "\177");
	expect_cursor(master, terminal, 11, 61);
	press(master, terminal, "\033OD");
	expect_cursor(master, terminal, 11, 60);
	press(master, terminal, "\033OC");
	expect_cursor(master, terminal, 11, 61);
	for (int i = 0; i < 14; i++) {
		press(master, terminal, "x");
		int column = 62 + i;
		if (column > 73)
			column = 73;
		expect_cursor(master, terminal, 11, column);
	}
	press(master, terminal, "\033OP");
	expect_cursor(master, terminal, 11, 73);
	check_compose(&ctx, master, terminal);
	check_compose_scroll(&ctx, master, terminal);
	check_compose_events(&ctx, master, terminal);
	check_compose_states(&ctx, master, terminal);
	check_compose_layout(&ctx, master, terminal);
	check_compose_spinbox(&ctx, master, terminal);
	check_compose_textview(&ctx, master, terminal);
	check_compose_changes(&ctx, master, terminal);
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
