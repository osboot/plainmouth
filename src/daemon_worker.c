// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/socket.h>
#include <sys/queue.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <pthread.h>

#include "daemon_worker.h"
#include "daemon_task.h"
#include "ipc.h"

struct worker {
	LIST_ENTRY(worker)
	entries;
	pthread_t thread;
	struct ipc_ctx *ctx;
	int socket_fd;
	bool done;
};
LIST_HEAD(worker_list, worker);
static struct worker_list workers = LIST_HEAD_INITIALIZER(workers);
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *connection_loop(void *data)
{
	struct worker *worker = data;
	ipc_event_loop(worker->ctx);
	ipc_free(worker->ctx);
	free(worker->ctx);
	pthread_mutex_lock(&mutex);
	close(worker->socket_fd);
	worker->socket_fd = -1;
	worker->done = true;
	pthread_mutex_unlock(&mutex);
	daemon_task_wakeup();
	return NULL;
}

bool daemon_worker_start(struct ipc_ctx *ctx)
{
	struct worker *worker = calloc(1, sizeof(*worker));
	if (!worker) {
		warn("calloc(worker)");
		goto fail;
	}
	worker->ctx = ctx;
	/* IPC may close its own fd; pin the socket to prevent fd reuse at stop. */
	worker->socket_fd = fcntl(ctx->fd, F_DUPFD_CLOEXEC, 0);
	if (worker->socket_fd < 0) {
		warn("fcntl(F_DUPFD_CLOEXEC)");
		free(worker);
		goto fail;
	}
	int rc = pthread_create(&worker->thread, NULL, connection_loop, worker);
	if (rc) {
		warnx("pthread_create: %s", strerror(rc));
		close(worker->socket_fd);
		free(worker);
		goto fail;
	}
	LIST_INSERT_HEAD(&workers, worker, entries);
	return true;
fail:
	ipc_free(ctx);
	free(ctx);
	return false;
}

static void join_worker(struct worker *worker)
{
	int rc = pthread_join(worker->thread, NULL);
	if (rc)
		errx(EXIT_FAILURE, "pthread_join: %s", strerror(rc));
	LIST_REMOVE(worker, entries);
	free(worker);
}

void daemon_workers_reap(void)
{
	struct worker *worker = LIST_FIRST(&workers);
	while (worker) {
		struct worker *next = LIST_NEXT(worker, entries);
		pthread_mutex_lock(&mutex);
		bool done = worker->done;
		pthread_mutex_unlock(&mutex);
		if (done)
			join_worker(worker);
		worker = next;
	}
}

void daemon_workers_stop(void)
{
	pthread_mutex_lock(&mutex);
	struct worker *worker;
	LIST_FOREACH(worker, &workers, entries)
	{
		if (worker->socket_fd >= 0 && shutdown(worker->socket_fd, SHUT_RDWR) < 0 && errno != ENOTCONN)
			warn("shutdown(client)");
	}
	pthread_mutex_unlock(&mutex);
	while ((worker = LIST_FIRST(&workers)))
		join_worker(worker);
	pthread_mutex_destroy(&mutex);
}
