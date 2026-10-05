// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <err.h>

#include "ipc.h"

enum dialog_type {
	DIALOG_MSGBOX,
	DIALOG_YESNO,
	DIALOG_INPUTBOX,
	DIALOG_PASSWORDBOX,
	DIALOG_TIMEBOX,
	DIALOG_RANGEBOX,
	DIALOG_GAUGE,
	DIALOG_MENU,
	DIALOG_CHECKLIST,
	DIALOG_RADIOLIST,
	DIALOG_TAILBOX,
	DIALOG_TEXTBOX,
	DIALOG_TERMBOX,
	DIALOG_COUNT,
};

enum button_label {
	LABEL_NONE,
	LABEL_OK,
	LABEL_CANCEL,
	LABEL_YES,
	LABEL_NO,
	LABEL_EXIT,
	LABEL_COUNT,
};

static const struct {
	const char *option;
	const char *text;
} button_labels[LABEL_COUNT] = {
	[LABEL_OK]     = { "--ok-label",     "OK"     },
	[LABEL_CANCEL] = { "--cancel-label", "Cancel" },
	[LABEL_YES]    = { "--yes-label",    "Yes"    },
	[LABEL_NO]     = { "--no-label",     "No"     },
	[LABEL_EXIT]   = { "--exit-label",   "EXIT"   },
};

struct dialog_spec {
	const char *option;
	const char *plugin;
	const char *content_field;
	const char *result_key;
	enum button_label accept_label;
	enum button_label cancel_label;
};

static const struct dialog_spec dialogs[DIALOG_COUNT] = {
	[DIALOG_MSGBOX]      = { "--msgbox",      "msgbox",    "text",    NULL,         LABEL_OK,   LABEL_NONE   },
	[DIALOG_YESNO]       = { "--yesno",       "msgbox",    "text",    NULL,         LABEL_YES,  LABEL_NO     },
	[DIALOG_INPUTBOX]    = { "--inputbox",    "inputbox",  "text",    "INPUT_1",    LABEL_OK,   LABEL_CANCEL },
	[DIALOG_PASSWORDBOX] = { "--passwordbox", "password",  "text",    "PASSWORD_1", LABEL_OK,   LABEL_CANCEL },
	[DIALOG_TIMEBOX]     = { "--timebox",     "timebox",   "text",    NULL,         LABEL_OK,   LABEL_CANCEL },
	[DIALOG_RANGEBOX]    = { "--rangebox",    "rangebox",  "text",    "VALUE",      LABEL_OK,   LABEL_CANCEL },
	[DIALOG_GAUGE]       = { "--gauge",       "gauge",     "text",    NULL,         LABEL_NONE, LABEL_NONE   },
	[DIALOG_MENU]        = { "--menu",        "menu",      "text",    "SELECTED",   LABEL_OK,   LABEL_CANCEL },
	[DIALOG_CHECKLIST]   = { "--checklist",   "checklist", "text",    NULL,         LABEL_OK,   LABEL_CANCEL },
	[DIALOG_RADIOLIST]   = { "--radiolist",   "checklist", "text",    NULL,         LABEL_OK,   LABEL_CANCEL },
	[DIALOG_TAILBOX]     = { "--tailbox",     "tailbox",   "file",    NULL,         LABEL_EXIT, LABEL_NONE   },
	[DIALOG_TEXTBOX]     = { "--textbox",     "textbox",   "file",    NULL,         LABEL_EXIT, LABEL_NONE   },
	[DIALOG_TERMBOX]     = { "--termbox",     "termbox",   "command", NULL,         LABEL_OK,   LABEL_NONE   },
};

static enum button_label find_button_label(const char *option)
{
	for (enum button_label label = LABEL_OK; label < LABEL_COUNT; label++)
		if (!strcmp(button_labels[label].option, option))
			return label;

	return LABEL_NONE;
}

static enum dialog_type find_dialog(const char *option)
{
	for (enum dialog_type type = 0; type < DIALOG_COUNT; type++)
		if (!strcmp(dialogs[type].option, option))
			return type;

	return DIALOG_COUNT;
}

static volatile sig_atomic_t interrupted;
static volatile sig_atomic_t active_fd = -1;

static void interrupt_handler(int signo)
{
	int saved_errno = errno;
	interrupted = signo;
	if (active_fd >= 0) {
		close((int) active_fd);
		active_fd = -1;
	}
	errno = saved_errno;
}

static bool parse_number(const char *s, long *value)
{
	char *end;
	errno = 0;
	long n = strtol(s, &end, 10);
	if (!*s || *end || errno || n < INT_MIN || n > INT_MAX)
		return false;
	*value = n;
	return true;
}

static bool positive_number(const char *s, long *value)
{
	return parse_number(s, value) && *value > 0;
}

static const char *get_result(const struct ipc_pair *pairs, const char *key)
{
	for (size_t i = 0; i < pairs->num_kv; i++)
		if (!strcmp(pairs->kv[i].key, key))
			return pairs->kv[i].val;

	return NULL;
}

static bool action(struct ipc_ctx *ctx, const char *id, const char *name,
		   struct ipc_pair *result)
{
	struct ipc_pair request = { 0 };
	bool ok = ipc_pair_add(&request, "action", name) &&
		  ipc_pair_add(&request, "id", id) &&
		  ipc_send_message2(ctx, &request, result);

	ipc_pair_free(&request);

	return ok;
}

static bool output_quoted(FILE *output, const char *text)
{
	if (fputc('"', output) == EOF)
		return false;

	for (; *text; text++) {
		if ((*text == '"' || *text == '\\') && fputc('\\', output) == EOF)
			return false;
		if (fputc((unsigned char) *text, output) == EOF)
			return false;
	}

	return fputc('"', output) != EOF;
}

enum gauge_state {
	GAUGE_PERCENT,
	GAUGE_BLOCK_PERCENT,
	GAUGE_BLOCK_TEXT,
};

struct gauge_input {
	enum gauge_state state;
	long value;
	size_t text_len;
	char text[8192];
};

static bool gauge_line(struct ipc_ctx *ctx, const char *id,
		       struct gauge_input *input, const char *line)
{
	if (input->state == GAUGE_BLOCK_TEXT && strcmp(line, "XXX")) {
		size_t len = strlen(line);

		if (input->text_len + len + 1 >= sizeof(input->text))
			return false;

		memcpy(input->text + input->text_len, line, len);
		input->text_len += len;
		input->text[input->text_len++] = '\n';

		return true;
	}
	if (input->state == GAUGE_PERCENT && !strcmp(line, "XXX")) {
		input->state = GAUGE_BLOCK_PERCENT;
		input->text_len = 0;

		return true;
	}
	if (input->state != GAUGE_BLOCK_TEXT) {
		if (!parse_number(line, &input->value) || input->value < 0 || input->value > 100)
			return false;

		if (input->state == GAUGE_BLOCK_PERCENT) {
			input->state = GAUGE_BLOCK_TEXT;
			return true;
		}
	}

	struct ipc_pair request = { 0 };
	bool ok = ipc_pair_add(&request, "action", "update") &&
		  ipc_pair_add(&request, "id", id) &&
		  ipc_pair_sprintf(&request, "value", "%ld", input->value);

	if (ok && input->state == GAUGE_BLOCK_TEXT) {
		if (input->text_len)
			input->text_len--;
		input->text[input->text_len] = '\0';
		ok = ipc_pair_add(&request, "text", input->text);
	}

	if (ok && !interrupted)
		ok = ipc_send_message2(ctx, &request, NULL);
	else
		ok = false;

	ipc_pair_free(&request);
	input->state = GAUGE_PERCENT;

	return ok;
}

static bool gauge_stream(struct ipc_ctx *ctx, const char *id)
{
	struct gauge_input input = { 0 };
	char line[8192], buffer[1024];
	size_t len = 0;
	struct pollfd fd = { .fd = STDIN_FILENO, .events = POLLIN };

	while (!interrupted) {
		int ready = poll(&fd, 1, 100);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			warn("waiting for gauge input");
			return false;
		}

		if (!ready || interrupted)
			continue;

		ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			warn("reading gauge input");
			return false;
		}
		if (!n) {
			if (len) {
				line[len] = '\0';
				if (!gauge_line(ctx, id, &input, line))
					break;
			}
			if (input.state == GAUGE_PERCENT)
				return !interrupted;
			break;
		}

		for (ssize_t i = 0; i < n; i++) {
			if (buffer[i] == '\n') {
				if (len && line[len - 1] == '\r')
					len--;
				line[len] = '\0';
				if (!gauge_line(ctx, id, &input, line))
					goto invalid;
				len = 0;
			} else {
				if (!buffer[i] || len + 1 >= sizeof(line))
					goto invalid;
				line[len++] = buffer[i];
			}
		}
	}
invalid:
	if (!interrupted)
		warnx("invalid gauge input or update failure");
	return false;
}

int main(int argc, char **argv)
{
	const char *socket_file = getenv("PLAINMOUTH_SOCKET");
	FILE *output = stderr;
	bool separate_output = false;
	const char *output_separator = NULL;
	const char *labels[LABEL_COUNT];
	for (enum button_label label = LABEL_NONE; label < LABEL_COUNT; label++)
		labels[label] = button_labels[label].text;
	int i = 1;
	while (i < argc) {
		enum button_label label = find_button_label(argv[i]);
		if (label != LABEL_NONE) {
			if (i + 1 >= argc)
				goto invalid_arguments;
			labels[label] = argv[++i];
		} else if (!strcmp(argv[i], "--stdout"))
			output = stdout;
		else if (!strcmp(argv[i], "--stderr"))
			output = stderr;
		else if (!strcmp(argv[i], "--separate-output"))
			separate_output = true;
		else if (!strcmp(argv[i], "--output-separator") || !strcmp(argv[i], "--separator")) {
			if (i + 1 >= argc)
				goto invalid_arguments;
			output_separator = argv[++i];
		} else if (!strcmp(argv[i], "--socket-file") && i + 1 < argc)
			socket_file = argv[++i];
		else if (!strcmp(argv[i], "--help")) {
			puts("Usage: plaindialog [--socket-file PATH] [--stdout|--stderr]\n"
			     "       [--separate-output] [--output-separator STRING|--separator STRING]\n"
			     "       [--ok-label TEXT] [--cancel-label TEXT]\n"
			     "       [--yes-label TEXT] [--no-label TEXT] [--exit-label TEXT]\n"
			     "       --msgbox|--yesno TEXT HEIGHT WIDTH\n"
			     "       --tailbox FILE HEIGHT WIDTH\n"
			     "       --textbox FILE HEIGHT WIDTH\n"
			     "       --termbox COMMAND HEIGHT WIDTH\n"
			     "       --inputbox TEXT HEIGHT WIDTH [INIT]\n"
			     "       --passwordbox TEXT HEIGHT WIDTH [INIT]\n"
			     "       --timebox TEXT HEIGHT WIDTH HOUR MINUTE SECOND\n"
			     "       --rangebox TEXT HEIGHT WIDTH MIN MAX VALUE\n"
			     "       --gauge TEXT HEIGHT WIDTH [PERCENT]\n"
			     "       --menu TEXT HEIGHT WIDTH MENU_HEIGHT TAG ITEM ...\n"
			     "       --checklist|--radiolist TEXT HEIGHT WIDTH LIST_HEIGHT\n"
			     "           TAG ITEM STATUS ...");
			return 0;
		} else
			break;
		i++;
	}

	enum dialog_type type = DIALOG_COUNT;

	if (i < argc)
		type = find_dialog(argv[i]);

	long height, width, visible = 0;
	long hour = 0, minute = 0, second = 0;
	long range_min = 0, range_max = 0, range_value = 0;
	long percent = 0;
	int remaining = argc - i;

	if (type == DIALOG_COUNT || remaining < 4 ||
	    (separate_output && type != DIALOG_CHECKLIST) ||
	    !positive_number(argv[i + 2], &height) ||
	    !positive_number(argv[i + 3], &width))
		goto invalid_arguments;

	switch (type) {
		case DIALOG_INPUTBOX:
		case DIALOG_PASSWORDBOX:
			if (remaining != 4 && remaining != 5)
				goto invalid_arguments;
			break;
		case DIALOG_GAUGE:
			if ((remaining != 4 && remaining != 5) ||
			    (remaining == 5 && (!parse_number(argv[i + 4], &percent) || percent < 0 || percent > 100)))
				goto invalid_arguments;
			break;
		case DIALOG_RANGEBOX:
			if (remaining != 7 || !parse_number(argv[i + 4], &range_min) ||
			    !parse_number(argv[i + 5], &range_max) || !parse_number(argv[i + 6], &range_value) ||
			    range_min > range_max || range_value < range_min || range_value > range_max)
				goto invalid_arguments;
			break;
		case DIALOG_TIMEBOX:
			if (remaining != 7 || !parse_number(argv[i + 4], &hour) ||
			    !parse_number(argv[i + 5], &minute) || !parse_number(argv[i + 6], &second) ||
			    hour >= 24 || minute >= 60 || second >= 60)
				goto invalid_arguments;
			break;
		case DIALOG_MENU:
			if (remaining < 7 || (remaining - 5) % 2 || !positive_number(argv[i + 4], &visible))
				goto invalid_arguments;
			break;
		case DIALOG_CHECKLIST:
		case DIALOG_RADIOLIST:
			if (remaining < 8 || (remaining - 5) % 3 || !positive_number(argv[i + 4], &visible))
				goto invalid_arguments;
			break;
		default:
			if (remaining != 4)
				goto invalid_arguments;
			break;
	}

	const struct dialog_spec *spec = &dialogs[type];
	const char *accept_button = labels[spec->accept_label];
	const char *cancel_button = labels[spec->cancel_label];

	if (type == DIALOG_TIMEBOX && (hour < 0 || minute < 0 || second < 0)) {
		time_t now = time(NULL);
		struct tm current;
		if (now == (time_t) -1 || !localtime_r(&now, &current)) {
			warn("unable to read current time");
			return 255;
		}
		if (hour < 0)
			hour = current.tm_hour;
		if (minute < 0)
			minute = current.tm_min;
		if (second < 0)
			second = current.tm_sec;
	}

	if (!socket_file || !*socket_file) {
		warnx("set PLAINMOUTH_SOCKET or use --socket-file");
		return 255;
	}

	struct sigaction sa = { .sa_handler = interrupt_handler };
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL) ||
	    sigaction(SIGHUP, &sa, NULL)) {
		warn("sigaction");
		return 255;
	}

	sa.sa_handler = SIG_IGN;

	if (sigaction(SIGPIPE, &sa, NULL)) {
		warn("sigaction");
		return 255;
	}

	char id[64];
	snprintf(id, sizeof(id), "plaindialog-%ld", (long) getpid());

	struct ipc_ctx ctx;
	ipc_init(&ctx);

	struct ipc_pair request = { 0 }, result = { 0 };

	bool created = false;
	bool create_sent = false;
	int status = 255;

	if (!ipc_connect(&ctx, socket_file, 0))
		goto out;

	active_fd = ctx.fd;

	if (interrupted)
		goto out;

	if (!ipc_pair_add(&request, "action", "create") ||
	    !ipc_pair_add(&request, "id", id) ||
	    !ipc_pair_add(&request, "plugin", spec->plugin) ||
	    !ipc_pair_sprintf(&request, "height", "%ld", height) ||
	    !ipc_pair_sprintf(&request, "width", "%ld", width) ||
	    !ipc_pair_add(&request, "border", "true") ||
	    !ipc_pair_add(&request, spec->content_field, argv[i + 1]) ||
	    (accept_button && !ipc_pair_add(&request, "button", accept_button)) ||
	    (cancel_button && !ipc_pair_add(&request, "button", cancel_button)))
		goto out;

	switch (type) {
		case DIALOG_GAUGE:
			if (!ipc_pair_sprintf(&request, "value", "%ld", percent))
				goto out;
			break;
		case DIALOG_INPUTBOX:
		case DIALOG_PASSWORDBOX:
			if (remaining == 5 && !ipc_pair_add(&request, "value", argv[i + 4]))
				goto out;
			break;
		case DIALOG_RANGEBOX:
			if (!ipc_pair_sprintf(&request, "min", "%ld", range_min) ||
			    !ipc_pair_sprintf(&request, "max", "%ld", range_max) ||
			    !ipc_pair_sprintf(&request, "value", "%ld", range_value))
				goto out;
			break;
		case DIALOG_TIMEBOX:
			if (!ipc_pair_sprintf(&request, "hour", "%ld", hour) ||
			    !ipc_pair_sprintf(&request, "minute", "%ld", minute) ||
			    !ipc_pair_sprintf(&request, "second", "%ld", second))
				goto out;
			break;
		case DIALOG_MENU:
			if (!ipc_pair_sprintf(&request, "visible", "%ld", visible))
				goto out;
			for (int n = i + 5; n < argc; n += 2) {
				if (!ipc_pair_sprintf(&request, "option", "%s  %s", argv[n], argv[n + 1]))
					goto out;
			}
			break;
		case DIALOG_CHECKLIST:
		case DIALOG_RADIOLIST:
			if (!ipc_pair_sprintf(&request, "visible", "%ld", visible) ||
			    (type == DIALOG_CHECKLIST && !ipc_pair_sprintf(&request, "select", "%d", (remaining - 5) / 3)) ||
			    (type == DIALOG_RADIOLIST && !ipc_pair_add(&request, "select", "1")))
				goto out;
			for (int n = i + 5; n < argc; n += 3) {
				const char *initial;
				if (!strcasecmp(argv[n + 2], "on"))
					initial = "true";
				else if (!strcasecmp(argv[n + 2], "off"))
					initial = "false";
				else {
					warnx("invalid status: %s", argv[n + 2]);
					goto out;
				}
				if (!ipc_pair_sprintf(&request, "option", "%s  %s", argv[n], argv[n + 1]) ||
				    !ipc_pair_add(&request, "status", initial))
					goto out;
			}
			break;
		default:
			break;
	}

	create_sent = true;
	if (!ipc_send_message2(&ctx, &request, &result))
		goto out;

	created = true;
	ipc_pair_free(&result);
	memset(&result, 0, sizeof(result));

	if (type == DIALOG_GAUGE) {
		if (gauge_stream(&ctx, id))
			status = 0;
		goto out;
	}

	if (interrupted || !action(&ctx, id, "focus", NULL) ||
	    !action(&ctx, id, "wait-result", &result) || interrupted)
		goto out;

	const char *cancel = get_result(&result, "BUTTON_2");
	status = cancel && !strcmp(cancel, "1") ? 1 : 0;

	if (status)
		goto out;

	switch (type) {
		case DIALOG_INPUTBOX:
		case DIALOG_PASSWORDBOX:
		case DIALOG_MENU:
		case DIALOG_RANGEBOX: {
			const char *value = get_result(&result, spec->result_key);
			long selected;

			if (!value || (type == DIALOG_RANGEBOX && (!parse_number(value, &range_value) || range_value < range_min || range_value > range_max)) ||
			    (type == DIALOG_MENU && (!positive_number(value, &selected) || selected > (remaining - 5) / 2))) {
				warnx("invalid plugin result");
				status = 255;
			} else {
				if (type == DIALOG_MENU)
					value = argv[i + 5 + 2 * ((int) selected - 1)];

				if (fputs(value, output) == EOF || fflush(output) == EOF)
					status = 255;
			}
			break;
		}
		case DIALOG_TIMEBOX: {
			const char *h = get_result(&result, "SPINBOX_HOURS");
			const char *m = get_result(&result, "SPINBOX_MINUTES");
			const char *s = get_result(&result, "SPINBOX_SECONDS");

			if (!h || !m || !s || !parse_number(h, &hour) || !parse_number(m, &minute) ||
			    !parse_number(s, &second) || hour < 0 || hour > 23 || minute < 0 ||
			    minute > 59 || second < 0 || second > 59) {
				warnx("invalid plugin result");
				status = 255;
			} else if (fprintf(output, "%02ld:%02ld:%02ld", hour, minute, second) < 0 ||
				   fflush(output) == EOF)
				status = 255;
			break;
		}
		case DIALOG_CHECKLIST:
		case DIALOG_RADIOLIST: {
			bool first = true;
			bool separate = type == DIALOG_CHECKLIST && separate_output;
			const char *separator = output_separator;
			if (!separator) {
				separator = " ";
				if (separate)
					separator = "\n";
			}
			int options = (remaining - 5) / 3;

			for (int n = 1; n <= options; n++) {
				char key[64];
				snprintf(key, sizeof(key), "SELECT_1_OPTION_%d", n);
				const char *selected = get_result(&result, key);

				if (!selected || (strcmp(selected, "0") && strcmp(selected, "1"))) {
					warnx("invalid plugin result");
					status = 255;
					break;
				}

				if (!strcmp(selected, "0"))
					continue;

				const char *tag = argv[i + 5 + 3 * (n - 1)];
				bool ok;

				if (separate)
					ok = fputs(tag, output) != EOF && fputs(separator, output) != EOF;
				else {
					ok = true;
					if (!first || output_separator)
						ok = fputs(separator, output) != EOF;
					if (ok && type == DIALOG_RADIOLIST)
						ok = fputs(tag, output) != EOF;
					else if (ok)
						ok = output_quoted(output, tag);
				}

				if (!ok) {
					status = 255;
					break;
				}
				first = false;
			}
			if (status != 255 && fflush(output) == EOF)
				status = 255;
			break;
		}
		default:
			break;
	}
out:
	if (status == 255 && !interrupted) {
		const char *error = get_result(&result, "ERR");
		if (error)
			warnx("%s", error);
	}

	/* A signal closes the waiting connection; cleanup uses a fresh one. */
	if (interrupted && active_fd < 0)
		ctx.fd = -1;

	active_fd = -1;
	ipc_free(&ctx);

	if (created || (create_sent && !get_result(&result, "ERR"))) {
		ipc_init(&ctx);
		if (!ipc_connect(&ctx, socket_file, 0) || !action(&ctx, id, "delete", NULL))
			status = 255;
		ipc_free(&ctx);
	}
	ipc_pair_free(&request);
	ipc_pair_free(&result);
	return interrupted ? 255 : status;

invalid_arguments:
	warnx("invalid or unsupported arguments; see --help");
	return 255;
}
