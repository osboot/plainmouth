// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/socket.h>
#include <assert.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daemon_task.h"
#include "daemon_worker.h"
#include "ipc.h"

static int start_client(const char *input)
{
	int fd[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
	struct ipc_ctx *ctx = calloc(1, sizeof(*ctx));
	assert(ctx);
	ipc_init(ctx);
	ctx->fd = fd[0];
	assert(daemon_worker_start(ctx));
	if (input)
		assert(write(fd[1], input, strlen(input)) == (ssize_t) strlen(input));
	return fd[1];
}

int main(void)
{
	daemon_task_init();
	for (int i = 0; i < 16; i++) {
		int fd = start_client("HELLO\nTAKE incomplete\nPAIR incomplete text=value\n");
		close(fd);
		struct pollfd notification = { .fd = daemon_task_fd(), .events = POLLIN };
		assert(poll(&notification, 1, 1000) == 1);
		daemon_workers_reap();
		uint64_t value;
		assert(read(daemon_task_fd(), &value, sizeof(value)) == sizeof(value));
	}
	int idle = start_client(NULL);
	int partial = start_client("HELLO\nTAKE pending\nPAIR pending text=unfinished");
	daemon_task_stop();
	daemon_workers_stop();
	char buf[128];
	while (read(idle, buf, sizeof(buf)) > 0)
		;
	while (read(partial, buf, sizeof(buf)) > 0)
		;
	close(idle);
	close(partial);
	daemon_task_free();
	return 0;
}
