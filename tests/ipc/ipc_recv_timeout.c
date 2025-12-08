// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdio.h>

#include "ipc.h"

static void test_ipc_recv_timeout(void)
{
	struct ipc_ctx ctx;
	ipc_init(&ctx);

	const char *socket_path = "/tmp/test_ipc_socket1";

	assert(ipc_listen(&ctx, socket_path, 5, 0) == true);
	assert(ipc_recv_timeout(&ctx, 1) == true);

	char message[BUFSIZ];
	assert(ipc_recv_data(ctx.fd, message, sizeof(message)) == -1);

	ipc_free(&ctx);
}

int main(void)
{
	test_ipc_recv_timeout();
	return 0;
}
