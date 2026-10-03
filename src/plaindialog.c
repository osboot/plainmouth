// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static bool positive_number(const char *s, long *value)
{
	char *end;
	errno = 0;
	long n = strtol(s, &end, 10);
	if (!*s || *end || errno || n <= 0 || n > INT_MAX)
		return false;
	*value = n;
	return true;
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
			     "       --termbox COMMAND HEIGHT WIDTH\n"
			     "       --inputbox TEXT HEIGHT WIDTH [INIT]\n"
			     "       --menu TEXT HEIGHT WIDTH MENU_HEIGHT TAG ITEM ...");
			return 0;
		} else
			break;
		i++;
	}

	bool menu = i < argc && !strcmp(argv[i], "--menu");
	bool input = i < argc && !strcmp(argv[i], "--inputbox");
	bool yesno = i < argc && !strcmp(argv[i], "--yesno");
	bool msgbox = i < argc && !strcmp(argv[i], "--msgbox");
	bool tailbox = i < argc && !strcmp(argv[i], "--tailbox");
	bool termbox = i < argc && !strcmp(argv[i], "--termbox");
	long height, width, visible = 0;
	int remaining = argc - i;
	if ((!menu && !input && !yesno && !msgbox && !tailbox && !termbox) || remaining < 4 ||
	    !positive_number(argv[i + 2], &height) ||
	    !positive_number(argv[i + 3], &width) ||
	    (input && remaining != 4 && remaining != 5) ||
	    ((yesno || msgbox || tailbox || termbox) && remaining != 4) ||
	    (menu && (remaining < 7 || (remaining - 5) % 2 ||
		      !positive_number(argv[i + 4], &visible)))) {
		warnx("invalid or unsupported arguments; see --help");
		return 255;
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

	} else if (menu) {
		plugin_name = "menu";

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
	    !ipc_pair_add(&request, "button", yesno ? "Yes" : "OK") ||
	    (!msgbox && !tailbox && !termbox && !ipc_pair_add(&request, "button", yesno ? "No" : "Cancel")))
		goto out;
	if (input && remaining == 5 && !ipc_pair_add(&request, "value", argv[i + 4]))
		goto out;
	if (menu) {
		if (!ipc_pair_sprintf(&request, "visible", "%ld", visible))
			goto out;
		for (int n = i + 5; n < argc; n += 2) {
			if (!ipc_pair_sprintf(&request, "option", "%s  %s", argv[n], argv[n + 1]))
				goto out;
		}
	}
	create_sent = true;
	if (!ipc_send_message2(&ctx, &request, &result))
		goto out;
	created = true;
	ipc_pair_free(&result);
	memset(&result, 0, sizeof(result));
	if (interrupted || !action(&ctx, id, "focus", NULL) ||
	    !action(&ctx, id, "wait-result", &result) || interrupted)
		goto out;
	const char *cancel = get_result(&result, "BUTTON_2");
	status = cancel && !strcmp(cancel, "1") ? 1 : 0;
	if (!status && (input || menu)) {
		const char *value = get_result(&result, input ? "INPUT_1" : "SELECTED");
		long selected;
		if (!value || (menu && (!positive_number(value, &selected) ||
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
