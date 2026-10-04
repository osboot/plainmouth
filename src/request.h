// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_REQUEST_H_
#define _PLAINMOUTH_REQUEST_H_

#include <stdbool.h>
#include <stdlib.h>

#include "ipc.h"

struct request {
	struct ipc_ctx     *r_ctx;
	struct ipc_message *r_msg;
	const struct widget *r_style_owner; /* Borrowed creation-time theme, set by the UI thread. */
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

const char *req_get_val(struct request *req, const char *key)             __attribute__((nonnull(1, 2)));
int req_get_int(struct request *req, const char *key, int def)            __attribute__((nonnull(1, 2)));
uint32_t req_get_uint(struct request *req, const char *key, uint32_t def) __attribute__((nonnull(1, 2)));
bool req_get_bool(struct request *req, const char *key, bool def)         __attribute__((nonnull(1, 2)));
wchar_t *req_get_kv_wchars(struct ipc_kv *kv)                             __attribute__((malloc, nonnull(1)));
wchar_t *req_get_wchars(struct request *req, const char *key)             __attribute__((malloc, nonnull(1, 2)));

bool req_error(struct request *req, const char *format, ...) __attribute__((format(printf, 2, 3)));
bool req_read_int(struct request *req, const char *key, int *value);
bool req_read_bool(struct request *req, const char *key, bool def, bool *value);
bool req_read_kv_bool(struct request *req, const struct ipc_kv *kv, bool *value);

#endif /* _PLAINMOUTH_REQUEST_H_ */
