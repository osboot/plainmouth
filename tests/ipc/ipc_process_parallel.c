// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <assert.h>

#include "ipc.h"

#define N_MSGS 5

static int print_message(struct ipc_ctx *ctx, struct ipc_message *m, void *data)
{
	int *counter = data;
	fprintf(stderr, "Message %s:\n", m->id);
	for (size_t i = 0; i < m->data.num_kv; i++)
		fprintf(stderr, "  %s='%s'\n", m->data.kv[i].key, m->data.kv[i].val);
	fflush(stderr);
	if (counter)
		*counter += 1;
	return 0;
}

static int event_loop_iter(void *data)
{
	int *counter = data;
	return *counter < N_MSGS;
}

int main(void)
{

	struct ipc_ctx ctx;
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
		perror("socketpair");
		return 1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid > 0) {
		int counter = 0;

		close(sv[0]);

		ipc_init(&ctx);
		ctx.fd = sv[1];
		ctx.data = &counter;
		ctx.event_loop_iter = event_loop_iter;
		ctx.handle_message = print_message;

		ipc_event_loop(&ctx);
		ipc_free(&ctx);
		return 0;
	} else {
		close(sv[1]);

		ipc_init(&ctx);
		ctx.fd = sv[0];
		ctx.handle_message = print_message;

		for (int i = 0; i < N_MSGS; i++) {
			assert(ipc_send_string(ctx.fd, "%s", "HELLO") > 0);
		}

		struct ipc_token response1[N_MSGS];
		struct ipc_token response2[N_MSGS];

		memset(&response1, 0, sizeof(response1));
		memset(&response2, 0, sizeof(response2));

		for (int i = 0; i < N_MSGS; i++) {
			assert(ipc_recv_token(&ctx, &response1[i]) > 0);
			assert(!strcmp(response1[i].cmd, "TAKE"));
		}

		char *pairs[] = {
			(char *) "name=example",
			(char *) "lang=C",
			(char *) "note=hello\nworld",
		};

		for (int n = 0; n < 3; n++) {
			for (int i = 0; i < N_MSGS; i++) {
				assert(ipc_send_string(ctx.fd, "PAIR %s %s-%d", response1[i].id, pairs[n], i) > 0);
			}
		}

		for (int i = 0; i < N_MSGS; i++) {
			assert(ipc_send_string(ctx.fd, "DONE %s", response1[i].id) > 0);
		}

		for (int i = 0; i < N_MSGS; i++) {
			assert(ipc_recv_token(&ctx, &response2[i]) > 0);
			assert(!strcmp(response2[i].cmd, "RESPONSE"));
			assert(!strcmp(response2[i].status, "OK"));
		}

		for (int i = 0; i < N_MSGS; i++) {
			ipc_free_token(&response1[i]);
			ipc_free_token(&response2[i]);
		}

		ipc_free(&ctx);
		return 0;
	}
}
