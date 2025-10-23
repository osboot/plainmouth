// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include "ipc.h"

static void test_ipc_init_and_free(void)
{
	struct ipc_ctx ctx;
	ipc_init(&ctx);

	assert(ctx.sockfd == -1);
	assert(ctx.fd == -1);
	assert(ctx.inbuf.data == NULL);
	assert(ctx.inbuf.len == 0);

	ipc_free(&ctx);
	assert(ctx.sockfd == -1);
	assert(ctx.fd == -1);
}

int main(void)
{
	test_ipc_init_and_free();
	return 0;
}
