// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdlib.h>
#include <assert.h>
#include "ipc.h"

static void test_ipc_listen_and_accept(void)
{
	struct ipc_ctx server_ctx, client_ctx, *ctx;
	ipc_init(&server_ctx);
	ipc_init(&client_ctx);

	const char *socket_path = "/tmp/test_ipc_socket2";

	assert(ipc_listen(&server_ctx, socket_path, 5, 0) == true);
	assert(ipc_connect(&client_ctx, socket_path, 0) == true);

	ctx = ipc_accept(&server_ctx);
	assert(ctx != NULL);

	assert(server_ctx.fd >= 0);
	assert(client_ctx.fd >= 0);

	ipc_free(&server_ctx);
	ipc_free(&client_ctx);

	ipc_free(ctx);
	free(ctx);
}

int main(void)
{
	test_ipc_listen_and_accept();
	return 0;
}
