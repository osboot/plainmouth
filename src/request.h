// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_REQUEST_H_
#define _PLAINMOUTH_REQUEST_H_

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

#endif /* _PLAINMOUTH_REQUEST_H_ */
