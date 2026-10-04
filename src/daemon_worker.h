// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_WORKER_H
#define PLAINMOUTH_DAEMON_WORKER_H

#include <stdbool.h>
struct ipc_ctx;

/* UI-thread only. Start consumes the accepted context, including on failure. */
bool daemon_worker_start(struct ipc_ctx *ctx);
void daemon_workers_reap(void);
/* Stop task/instance waits before interrupting sockets and joining workers. */
void daemon_workers_stop(void);
#endif
