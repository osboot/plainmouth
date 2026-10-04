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
	int i = 1;
	while (i < argc) {
		if (!strcmp(argv[i], "--stdout"))
			output = stdout;
		else if (!strcmp(argv[i], "--stderr"))
			output = stderr;
		else if (!strcmp(argv[i], "--socket-file") && i + 1 < argc)
			socket_file = argv[++i];
		else if (!strcmp(argv[i], "--help")) {
			puts("Usage: plaindialog [--socket-file PATH] [--stdout|--stderr]\n"
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

	bool menu = i < argc && !strcmp(argv[i], "--menu");
	bool checklist = i < argc && !strcmp(argv[i], "--checklist");
	bool radiolist = i < argc && !strcmp(argv[i], "--radiolist");
	bool input = i < argc && !strcmp(argv[i], "--inputbox");
	bool password = i < argc && !strcmp(argv[i], "--passwordbox");
	bool timebox = i < argc && !strcmp(argv[i], "--timebox");
	bool rangebox = i < argc && !strcmp(argv[i], "--rangebox");
	bool gauge = i < argc && !strcmp(argv[i], "--gauge");
	bool yesno = i < argc && !strcmp(argv[i], "--yesno");
	bool msgbox = i < argc && !strcmp(argv[i], "--msgbox");
	bool tailbox = i < argc && !strcmp(argv[i], "--tailbox");
	bool textbox = i < argc && !strcmp(argv[i], "--textbox");
	bool termbox = i < argc && !strcmp(argv[i], "--termbox");
	bool choice_list = checklist || radiolist;
	long height, width, visible = 0;
	long hour = 0, minute = 0, second = 0;
	long range_min = 0, range_max = 0, range_value = 0;
	long percent = 0;
	int remaining = argc - i;
	if ((!menu && !choice_list && !input && !password && !timebox && !rangebox && !gauge && !yesno && !msgbox && !tailbox && !textbox && !termbox) || remaining < 4 ||
	    !positive_number(argv[i + 2], &height) ||
	    !positive_number(argv[i + 3], &width) ||
	    (input && remaining != 4 && remaining != 5) ||
	    (password && remaining != 4 && remaining != 5) ||
	    (gauge && (remaining != 4 && remaining != 5)) ||
	    (gauge && remaining == 5 && (!parse_number(argv[i + 4], &percent) || percent < 0 || percent > 100)) ||
	    (rangebox && (remaining != 7 || !parse_number(argv[i + 4], &range_min) ||
			  !parse_number(argv[i + 5], &range_max) || !parse_number(argv[i + 6], &range_value) ||
			  range_min > range_max || range_value < range_min || range_value > range_max)) ||
	    (timebox && (remaining != 7 || !parse_number(argv[i + 4], &hour) ||
			 !parse_number(argv[i + 5], &minute) ||
			 !parse_number(argv[i + 6], &second) || hour >= 24 || minute >= 60 || second >= 60)) ||
	    ((yesno || msgbox || tailbox || textbox || termbox) && remaining != 4) ||
	    (menu && (remaining < 7 || (remaining - 5) % 2 ||
		      !positive_number(argv[i + 4], &visible))) ||
	    (choice_list && (remaining < 8 || (remaining - 5) % 3 ||
			     !positive_number(argv[i + 4], &visible)))) {
		warnx("invalid or unsupported arguments; see --help");
		return 255;
	}
	if (timebox && (hour < 0 || minute < 0 || second < 0)) {
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

	const char *plugin_name = "msgbox";
	const char *content_field = "text";

	if (termbox) {
		plugin_name = "termbox";
		content_field = "command";

	} else if (tailbox) {
		plugin_name = "tailbox";
		content_field = "file";

	} else if (textbox) {
		plugin_name = "textbox";
		content_field = "file";

	} else if (menu) {
		plugin_name = "menu";

	} else if (choice_list) {
		plugin_name = "checklist";

	} else if (password) {
		plugin_name = "password";

	} else if (timebox) {
		plugin_name = "timebox";

	} else if (rangebox) {
		plugin_name = "rangebox";

	} else if (gauge) {
		plugin_name = "gauge";

	} else if (input) {
		plugin_name = "inputbox";
	}

	if (!ipc_pair_add(&request, "action", "create") ||
	    !ipc_pair_add(&request, "id", id) ||
	    !ipc_pair_add(&request, "plugin", plugin_name) ||
	    !ipc_pair_sprintf(&request, "height", "%ld", height) ||
	    !ipc_pair_sprintf(&request, "width", "%ld", width) ||
	    !ipc_pair_add(&request, "border", "true") ||
	    !ipc_pair_add(&request, content_field, argv[i + 1]) ||
	    (!gauge && !ipc_pair_add(&request, "button", yesno ? "Yes" : "OK")) ||
	    (!gauge && !msgbox && !tailbox && !textbox && !termbox && !ipc_pair_add(&request, "button", yesno ? "No" : "Cancel")))
		goto out;
	if (gauge && !ipc_pair_sprintf(&request, "value", "%ld", percent))
		goto out;
	if (input && remaining == 5 && !ipc_pair_add(&request, "value", argv[i + 4]))
		goto out;
	if (password && remaining == 5 && !ipc_pair_add(&request, "value", argv[i + 4]))
		goto out;
	if (rangebox && (!ipc_pair_sprintf(&request, "min", "%ld", range_min) ||
			 !ipc_pair_sprintf(&request, "max", "%ld", range_max) ||
			 !ipc_pair_sprintf(&request, "value", "%ld", range_value)))
		goto out;
	if (timebox && (!ipc_pair_sprintf(&request, "hour", "%ld", hour) ||
			!ipc_pair_sprintf(&request, "minute", "%ld", minute) ||
			!ipc_pair_sprintf(&request, "second", "%ld", second)))
		goto out;
	if (menu) {
		if (!ipc_pair_sprintf(&request, "visible", "%ld", visible))
			goto out;
		for (int n = i + 5; n < argc; n += 2) {
			if (!ipc_pair_sprintf(&request, "option", "%s  %s", argv[n], argv[n + 1]))
				goto out;
		}
	}
	if (choice_list) {
		if (!ipc_pair_sprintf(&request, "visible", "%ld", visible) ||
		    (checklist && !ipc_pair_sprintf(&request, "select", "%d", (remaining - 5) / 3)) ||
		    (radiolist && !ipc_pair_add(&request, "select", "1")))
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
	}
	create_sent = true;
	if (!ipc_send_message2(&ctx, &request, &result))
		goto out;
	created = true;
	ipc_pair_free(&result);
	memset(&result, 0, sizeof(result));
	if (gauge) {
		if (gauge_stream(&ctx, id))
			status = 0;
		goto out;
	}
	if (interrupted || !action(&ctx, id, "focus", NULL) ||
	    !action(&ctx, id, "wait-result", &result) || interrupted)
		goto out;
	const char *cancel = get_result(&result, "BUTTON_2");
	status = cancel && !strcmp(cancel, "1") ? 1 : 0;
	if (!status && (input || password || menu || rangebox)) {
		const char *result_key = "SELECTED";
		if (input)
			result_key = "INPUT_1";
		else if (password)
			result_key = "PASSWORD_1";
		else if (rangebox)
			result_key = "VALUE";
		const char *value = get_result(&result, result_key);
		long selected;
		if (!value || (rangebox && (!parse_number(value, &range_value) || range_value < range_min || range_value > range_max)) ||
		    (menu && (!positive_number(value, &selected) ||
			      selected > (remaining - 5) / 2))) {
			warnx("invalid plugin result");
			status = 255;
		} else {
			if (menu)
				value = argv[i + 5 + 2 * ((int) selected - 1)];
			if (fputs(value, output) == EOF || fflush(output) == EOF)
				status = 255;
		}
	}
	if (!status && timebox) {
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
	}
	if (!status && choice_list) {
		bool first = true;
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
			bool ok = radiolist ? fputs(tag, output) != EOF : ((first || fputc(' ', output) != EOF) && output_quoted(output, tag));
			if (!ok) {
				status = 255;
				break;
			}
			first = false;
		}
		if (status != 255 && fflush(output) == EOF)
			status = 255;
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
}
