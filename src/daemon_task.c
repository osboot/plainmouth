// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/eventfd.h>
#include <sys/queue.h>

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <pthread.h>

#include "daemon_task.h"

struct task_state {
	struct ui_task task;
	TAILQ_ENTRY(task_state) entries;
	bool done;
	int rc;
};
TAILQ_HEAD(task_queue, task_state);

static struct task_queue tasks = TAILQ_HEAD_INITIALIZER(tasks);
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t ui_thread;
static int event_fd = -1;
static bool stopping;

void daemon_task_init(void)
{
	ui_thread = pthread_self();
	event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (event_fd < 0)
		err(EXIT_FAILURE, "eventfd");
}

void daemon_task_free(void)
{
	close(event_fd);
	event_fd = -1;
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

int daemon_task_fd(void)
{
	return event_fd;
}

static bool wakeup(void)
{
	uint64_t one = 1;
	ssize_t n;

	do {
		n = write(event_fd, &one, sizeof(one));
	} while (n < 0 && errno == EINTR);
	if (n == sizeof(one) || (n < 0 && errno == EAGAIN))
		return true;
	warn("write(eventfd)");
	return false;
}

void daemon_task_wakeup(void)
{
	wakeup();
}

struct ui_task *daemon_task_create(enum ui_task_type type, struct request *req)
{
	if (pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "daemon_task_create called from UI thread");

	struct task_state *state = calloc(1, sizeof(*state));
	if (!state) {
		warn("calloc(task_state)");
		return NULL;
	}
	state->task.type = type;
	state->task.req = *req;
	return &state->task;
}

int daemon_task_submit_and_wait(struct ui_task *task)
{
	if (pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "daemon_task_submit_and_wait called from UI thread");

	struct task_state *state = (struct task_state *) task;
	int rc;

	pthread_mutex_lock(&mutex);
	if (stopping) {
		pthread_mutex_unlock(&mutex);
		free(state);
		return -1;
	}
	TAILQ_INSERT_TAIL(&tasks, state, entries);
	if (!wakeup()) {
		TAILQ_REMOVE(&tasks, state, entries);
		pthread_mutex_unlock(&mutex);
		free(state);
		return -1;
	}
	while (!state->done)
		pthread_cond_wait(&cond, &mutex);
	rc = state->rc;
	pthread_mutex_unlock(&mutex);
	free(state);
	return rc;
}

void daemon_task_stop(void)
{
	pthread_mutex_lock(&mutex);
	stopping = true;

	struct task_state *state;
	while ((state = TAILQ_FIRST(&tasks))) {
		TAILQ_REMOVE(&tasks, state, entries);
		state->rc = -1;
		state->done = true;
	}

	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
}

void daemon_task_dispatch(int (*handler)(struct ui_task *task))
{
	if (!pthread_equal(pthread_self(), ui_thread))
		errx(EXIT_FAILURE, "daemon_task_dispatch called not from UI thread");

	uint64_t value;
	ssize_t n;
	do {
		n = read(event_fd, &value, sizeof(value));
	} while (n == sizeof(value) || (n < 0 && errno == EINTR));
	if (n < 0 && errno != EAGAIN)
		warn("read(eventfd)");

	pthread_mutex_lock(&mutex);
	struct task_state *state = TAILQ_FIRST(&tasks);
	TAILQ_INIT(&tasks);
	pthread_mutex_unlock(&mutex);

	while (state) {
		/* Completion lets the submitting worker free state immediately. */
		struct task_state *next = TAILQ_NEXT(state, entries);
		int rc = handler(&state->task);

		pthread_mutex_lock(&mutex);
		state->rc = rc;
		state->done = true;
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&mutex);
		state = next;
	}
}
