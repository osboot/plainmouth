// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "daemon_task.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool first_created;
static bool first_may_submit;
static bool first_returned;
static unsigned int handled;

static void *first_worker(void *data)
{
	struct request *req = data;
	struct ui_task *task = daemon_task_create(UI_TASK_CREATE, req);
	assert(task);
	pthread_mutex_lock(&mutex);
	first_created = true;
	pthread_cond_broadcast(&cond);
	while (!first_may_submit)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
	assert(daemon_task_submit_and_wait(task) == -17);
	pthread_mutex_lock(&mutex);
	first_returned = true;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
	return NULL;
}

static void *second_worker(void *data)
{
	struct ui_task *task = daemon_task_create(UI_TASK_RESULT, data);
	assert(task);
	assert(daemon_task_submit_and_wait(task) == 23);
	return NULL;
}

static int handle_task(struct ui_task *task)
{
	handled++;
	if (task->type == UI_TASK_RESULT) {
		assert(handled == 1);
		return 23;
	}
	assert(task->type == UI_TASK_CREATE);
	assert(handled == 2);

	/* A broadcast for the other task must not release this worker. */
	struct timespec deadline;
	assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
	deadline.tv_nsec += 100000000;
	if (deadline.tv_nsec >= 1000000000) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000;
	}
	pthread_mutex_lock(&mutex);
	int rc = 0;
	while (!first_returned && !rc)
		rc = pthread_cond_timedwait(&cond, &mutex, &deadline);
	assert(rc == ETIMEDOUT);
	assert(!first_returned);
	pthread_mutex_unlock(&mutex);
	return -17;
}

static void wait_for_submission(void)
{
	struct pollfd fd = { .fd = daemon_task_fd(), .events = POLLIN };
	assert(poll(&fd, 1, 1000) == 1);
	assert(fd.revents == POLLIN);
}

int main(void)
{
	struct request req = { 0 };
	pthread_t first, second;
	daemon_task_init();
	assert(pthread_create(&first, NULL, first_worker, &req) == 0);
	pthread_mutex_lock(&mutex);
	while (!first_created)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
	assert(pthread_create(&second, NULL, second_worker, &req) == 0);
	wait_for_submission();
	uint64_t value;
	assert(read(daemon_task_fd(), &value, sizeof(value)) == sizeof(value));
	pthread_mutex_lock(&mutex);
	first_may_submit = true;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
	wait_for_submission();
	daemon_task_dispatch(handle_task);
	assert(pthread_join(first, NULL) == 0);
	assert(pthread_join(second, NULL) == 0);
	assert(handled == 2);
	daemon_task_dispatch(handle_task);
	assert(handled == 2);
	daemon_task_free();
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
	return 0;
}
