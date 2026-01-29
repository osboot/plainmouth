// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <assert.h>

#include "macros.h"
#include "ipc.h"

static int print_message(struct ipc_ctx *ctx, struct ipc_message *m, void *data _UNUSED)
{
	(void) ctx;

	printf("Message %s:\n", m->id);
	for (size_t i = 0; i < m->data.num_kv; i++)
		printf("  %s='%s'\n", m->data.kv[i].key, m->data.kv[i].val);

	return 0;
}

int main(void)
{

	struct ipc_ctx ctx;
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		perror("socketpair");
		return 1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		close(sv[0]);

		ipc_init(&ctx);
		ctx.fd = sv[1];
		ctx.handle_message = print_message;

		ipc_event_loop(&ctx);
		ipc_free(&ctx);
		return 0;
	} else {
		close(sv[1]);

		ipc_init(&ctx);
		ctx.fd = sv[0];
		ctx.handle_message = print_message;

		char *pairs[] = {
			(char *) "name=example",
			(char *) "lang=C",
			(char *) "note=hello\nworld",
		};
		assert(ipc_send_message(&ctx, pairs, 3, NULL) == true);

		ipc_free(&ctx);
		return 0;
	}
}
