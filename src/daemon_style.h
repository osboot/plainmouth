// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_STYLE_H
#define PLAINMOUTH_DAEMON_STYLE_H

#include <stdbool.h>

struct request;
struct widget;

/* UI-thread only. Named sources outlive all instances using them. */
struct widget *daemon_style_find(const char *name);
/* The lookup reports missing instances. On success, NULL changed means global;
 * otherwise changed borrows the instance root or named source to redraw. */
bool daemon_style_apply(struct request *req, struct widget *(*lookup_instance)(void *),
			void *data, struct widget **changed);
void daemon_styles_free(void);

#endif
