// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ipc.h"

static void test_ipc_command_processing(void)
{
	struct ipc_ctx ctx;
	ipc_init(&ctx);

	struct ipc_token tok;

	char *line = strdup("TAKE 123");
	assert(ipc_parse_token(line, &tok) == 0);
	free(line);

	line = strdup("HELLO");
	assert(ipc_parse_token(line, &tok) == 0);
	free(line);

	line = strdup("UNKNOWN_COMMAND");
	assert(ipc_parse_token(line, &tok) < 0);
	free(line);

	ipc_free(&ctx);
}

int main(void)
{
	test_ipc_command_processing();
	return 0;
}
