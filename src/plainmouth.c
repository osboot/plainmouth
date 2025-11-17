// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <err.h>

#include "helpers.h"
#include "ipc.h"

static const char cmdopts_s[] = "S:Vh";
static const struct option cmdopts[] = {
	{ "has-active-vt", no_argument,       0, 1   },
	{ "quit",          no_argument,       0, 2   },
	{ "show-splash",   no_argument,       0, 3   },
	{ "hide-splash",   no_argument,       0, 4   },
	{ "ping",          no_argument,       0, 5   },
	{ "result",        no_argument,       0, 6   },
	{ "socket-file",   required_argument, 0, 'S' },
	{ "version",       no_argument,       0, 'V' },
	{ "help",          no_argument,       0, 'h' },
	{ 0,               0,                 0, 0   },
};

static void __attribute__((noreturn))
print_help(const char *progname, int retcode)
{
	printf("Usage: %s [options] <socket>\n"
	       "\n"
	       "Sends commands to a running server. This is used during the boot\n"
	       "process to control the display of the graphical boot splash.\n"
	       "\n"
	       "Options:\n"
	       "   --show-splash            Show the splash screen.\n"
	       "   --hide-splash            Hide the splash screen.\n"
	       "   --has-active-vt          Check if plainmouthd has an active vt.\n"
	       "   --quit                   Tell server to quit.\n"
	       "   --ping                   Check if plainmouthd is running.\n"
	       "   -S, --socket-file=FILE   Path to server socket file.\n"
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
	       "Written by Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "\n"
	       "Copyright (C) 2025  Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "This is free software; see the source for copying conditions.  There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	       "\n",
	       progname);
	exit(EXIT_SUCCESS);
}

static bool send_action(struct ipc_ctx *ctx, struct ipc_pair *resp, const char *action)
{
	struct ipc_pair data = { 0 };

	ipc_pair_sprintf(&data, "action", "%s", action);

	bool ret = ipc_send_message2(ctx, &data, resp);

	ipc_pair_free(&data);
	return ret;
}

#define send_ping(_ctx, _resp)		send_action(_ctx, _resp, "ping")
#define send_quit(_ctx, _resp)		send_action(_ctx, _resp, "quit")
#define send_is_active_vt(_ctx, _resp)	send_action(_ctx, _resp, "has-active-vt")
#define send_show_splash(_ctx, _resp)	send_action(_ctx, _resp, "show-splash")
#define send_hide_splash(_ctx, _resp)	send_action(_ctx, _resp, "hide-splash")

static int command_quit(struct ipc_ctx *ctx)
{
	struct ipc_pair resp = { 0 };
	bool ret = send_quit(ctx, &resp);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int command_ping(struct ipc_ctx *ctx)
{
	struct ipc_pair resp = { 0 };
	bool ret = send_ping(ctx, &resp);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	} else {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "pong"))
				ret = atoi(resp.kv[i].val) != 0;
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}


static int command_has_active_vt(struct ipc_ctx *ctx)
{
	struct ipc_pair resp = { 0 };
	bool ret = send_is_active_vt(ctx, &resp);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	} else {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "istty"))
				ret = atoi(resp.kv[i].val) != 0;
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int command_show_splash(struct ipc_ctx *ctx)
{
	struct ipc_pair resp = { 0 };
	bool ret = send_show_splash(ctx, &resp);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int command_hide_splash(struct ipc_ctx *ctx)
{
	struct ipc_pair resp = { 0 };
	bool ret = send_hide_splash(ctx, &resp);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (!strcasecmp(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int command_debug(struct ipc_ctx *ctx, int argc, char **argv)
{
	struct ipc_pair resp = { 0 };
	bool ret = ipc_send_message(ctx, argv, argc, &resp);

	for (size_t i = 0; i < resp.num_kv; i++)
		printf("%s=%s\n", resp.kv[i].key, resp.kv[i].val);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int command_result(struct ipc_ctx *ctx, int n_ids, char **ids)
{
	struct ipc_pair data = { 0 };
	struct ipc_pair resp = { 0 };

	ipc_pair_sprintf(&data, "action", "result");

	for (int i = 0; i < n_ids; i++)
		ipc_pair_sprintf(&data, "id", "%s", ids[i]);

	bool ret = ipc_send_message2(ctx, &data, &resp);

	ipc_pair_free(&data);

	if (!ret) {
		for (size_t i = 0; i < resp.num_kv; i++) {
			if (strcaseeq(resp.kv[i].key, "err"))
				warnx("%s", resp.kv[i].val);
		}
	} else {
		for (size_t i = 0; i < resp.num_kv; i++) {
			printf("%s=%s\n", resp.kv[i].key, resp.kv[i].val);
		}
	}

	ipc_pair_free(&resp);

	return (ret ? EXIT_SUCCESS : EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	int c;
	const char *socket_file = NULL;
	enum actions {
		DO_NOTHING        = 0,
		SRV_QUIT          = 1,
		SRV_HAS_ACTIVE_VT = 2,
		SRV_SHOW_SPLASH   = 3,
		SRV_HIDE_SPLASH   = 4,
		SRV_PING          = 5,
		SRV_RESULT        = 6,
	} action = DO_NOTHING;

	while ((c = getopt_long(argc, argv, cmdopts_s, cmdopts, NULL)) != -1) {
		switch (c) {
			case 1:
				action = SRV_HAS_ACTIVE_VT;
				break;
			case 2:
				action = SRV_QUIT;
				break;
			case 3:
				action = SRV_SHOW_SPLASH;
				break;
			case 4:
				action = SRV_HIDE_SPLASH;
				break;
			case 5:
				action = SRV_PING;
				break;
			case 6:
				action = SRV_RESULT;
				break;
			case 'S':
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

	int ret = EXIT_FAILURE;
	struct ipc_ctx ctx = { 0 };

	ipc_init(&ctx);
	ipc_connect(&ctx, socket_file, 0);

	switch (action) {
		case SRV_QUIT:
			ret = command_quit(&ctx);
			break;
		case SRV_HAS_ACTIVE_VT:
			ret = command_has_active_vt(&ctx);
			break;
		case SRV_SHOW_SPLASH:
			ret = command_show_splash(&ctx);
			break;
		case SRV_HIDE_SPLASH:
			ret = command_hide_splash(&ctx);
			break;
		case SRV_PING:
			ret = command_ping(&ctx);
			break;
		case SRV_RESULT:
			ret = command_result(&ctx, argc - optind, argv + optind);
			break;
		default:
			ret = command_debug(&ctx, argc - optind, argv + optind);
			break;
	}

	ipc_free(&ctx);
	fflush(stdout);

	return ret;
}
