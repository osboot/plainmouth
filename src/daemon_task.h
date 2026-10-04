// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_DAEMON_TASK_H_
#define _PLAINMOUTH_DAEMON_TASK_H_

#include "request.h"

enum ui_task_type {
	UI_TASK_NONE = 0,
	UI_TASK_DUMP,
	UI_TASK_CREATE,
	UI_TASK_UPDATE,
	UI_TASK_SET_VALUE,
	UI_TASK_DELETE,
	UI_TASK_FOCUS,
	UI_TASK_RESULT,
	UI_TASK_SHOW_SPLASH,
	UI_TASK_HIDE_SPLASH,
	UI_TASK_SET_TITLE,
	UI_TASK_SET_STYLE,
	UI_TASK_LIST_PLUGINS,
	UI_TASK_COUNT,
};

struct ui_task {
	enum ui_task_type type;
	struct request req;
};

/* Initialize/free on the UI thread; free only after joining all workers. */
void daemon_task_init(void);
void daemon_task_free(void);
/* UI-thread only; reject new submissions and release queued waiters. */
void daemon_task_stop(void);
int daemon_task_fd(void);
void daemon_task_wakeup(void);

/* The submitting worker retains the request data until wait returns. */
struct ui_task *daemon_task_create(enum ui_task_type type, struct request *req);
/* Consumes the task, including on submission failure. */
int daemon_task_submit_and_wait(struct ui_task *task);

/* Dispatch a snapshot on the UI thread; later submissions wait for the next call. */
void daemon_task_dispatch(int (*handler)(struct ui_task *task));

#endif /* _PLAINMOUTH_DAEMON_TASK_H_ */
