// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ipc.h"

static void test_ipc_handle_key_value_pairs(void)
{
	struct ipc_ctx ctx;
	ipc_init(&ctx);

	struct ipc_token tok;

	char *line = strdup("PAIR 1 key=value");
	assert(ipc_parse_token(line, &tok) == 0);
	assert(tok.handler != NULL);
	assert(tok.handler(&ctx, &tok) == true);
	free(line);

	struct ipc_message *msg = ipc_msg_find(&ctx, "1");
	assert(msg != NULL);
	assert(msg->data.num_kv == 1);
	assert(strcmp(msg->data.kv[0].key, "key") == 0);
	assert(strcmp(msg->data.kv[0].val, "value") == 0);

	ipc_free(&ctx);
}

int main(void)
{
	test_ipc_handle_key_value_pairs();
	return 0;
}
