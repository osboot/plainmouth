// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ipc.h"

static void test_ipc_send_and_receive(void)
{
	struct ipc_ctx server_ctx, client_ctx, *ctx;
	ipc_init(&server_ctx);
	ipc_init(&client_ctx);

	const char *socket_path = "/tmp/test_ipc_socket3";

	assert(ipc_listen(&server_ctx, socket_path, 5, 0) == true);
	assert(ipc_connect(&client_ctx, socket_path, 0) == true);

	ctx = ipc_accept(&server_ctx);
	assert(ctx != NULL);

	const char *message = "HELLO";
	assert(ipc_send_string(client_ctx.fd, "%s", message) > 0);

	char received_message[BUFSIZ];
	assert(ipc_recv_data(ctx->fd, received_message, sizeof(received_message)) > 0);
	assert(strcmp(received_message, "HELLO") == 0);

	ipc_free(&server_ctx);
	ipc_free(&client_ctx);

	ipc_free(ctx);
	free(ctx);
}

int main(void)
{
	test_ipc_send_and_receive();
	return 0;
}
