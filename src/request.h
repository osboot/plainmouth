// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_REQUEST_H_
#define _PLAINMOUTH_REQUEST_H_

#include <stdbool.h>

#include "ipc.h"

struct request {
	struct ipc_ctx     *r_ctx;
	struct ipc_message *r_msg;
};

static inline int req_fd(struct request *req)
{
	return req->r_ctx->fd;
}

static inline char *req_id(struct request *req)
{
	return req->r_msg->id;
}

static inline struct ipc_pair *req_data(struct request *req)
{
	return &req->r_msg->data;
}

const char *req_get_val(struct request *req, const char *key)     __attribute__((nonnull(1, 2)));
int req_get_int(struct request *req, const char *key, int def)    __attribute__((nonnull(1, 2)));
bool req_get_bool(struct request *req, const char *key, bool def) __attribute__((nonnull(1, 2)));

#endif /* _PLAINMOUTH_REQUEST_H_ */
