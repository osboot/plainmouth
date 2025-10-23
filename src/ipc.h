// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _IPC_H_
#define _IPC_H_

#include <sys/socket.h>
#include <sys/queue.h>

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

struct ipc_buffer {
	char *data;
	size_t len;
};

void ipc_buffer_free(struct ipc_buffer *a)                              __attribute__((nonnull(1)));
void ipc_buffer_append(struct ipc_buffer *a, const char *buf, size_t n) __attribute__((nonnull(1, 2)));
char *ipc_buffer_next_line(struct ipc_buffer *a)                        __attribute__((nonnull(1)));

struct ipc_kv {
	char *key;
	char *val;
};

struct ipc_pair {
	struct ipc_kv *kv;
	size_t num_kv;
	size_t capacity;
};

struct ipc_message {
	LIST_ENTRY(ipc_message) entries;

	char *id;
	struct ipc_pair data;
	struct ipc_pair resp;
};

LIST_HEAD(ipc_msg_list, ipc_message);

struct ipc_ctx {
	int fd;
	struct ipc_buffer inbuf;

	unsigned long next_msgid;
	struct ipc_msg_list msgs;

	void *data;
	int (*handle_message)(struct ipc_ctx *ctx, struct ipc_message *msg, void *ctx_data);
	int (*event_loop_iter)(void *ctx_data);
};

struct ipc_message *ipc_msg_find(struct ipc_ctx *ctx, const char *id) __attribute__((nonnull(1, 2)));
struct ipc_message *ipc_msg_add(struct ipc_ctx *ctx, const char *id)  __attribute__((nonnull(1, 2)));
void ipc_msg_free(struct ipc_message *m)                              __attribute__((nonnull(1)));

bool ipc_pair_add(struct ipc_pair *pair, const char *key, const char *val) __attribute__((nonnull(1, 2, 3)));
bool ipc_pair_sprintf(struct ipc_pair *pairs, const char *key, const char *fmt, ...)
			__attribute__((nonnull(1, 2), __format__(printf, 3, 4)));
void ipc_pair_free(struct ipc_pair *pair) __attribute__((nonnull(1)));

const char *ipc_get_val(const struct ipc_pair *p, const char *key)     __attribute__((nonnull(1, 2)));
int ipc_get_int(const struct ipc_pair *p, const char *key, int def)    __attribute__((nonnull(1, 2)));
bool ipc_get_bool(const struct ipc_pair *p, const char *key, bool def) __attribute__((nonnull(1, 2)));

void ipc_init(struct ipc_ctx *ctx) __attribute__((nonnull(1)));
void ipc_free(struct ipc_ctx *ctx) __attribute__((nonnull(1)));

void ipc_close(struct ipc_ctx *ctx) __attribute__((nonnull(1)));

bool ipc_connect(struct ipc_ctx *ctx, const char *file_name, int sock_flags)             __attribute__((nonnull(1, 2)));
bool ipc_listen(struct ipc_ctx *ctx, const char *file_name, int backlog, int sock_flags) __attribute__((nonnull(1, 2)));
struct ipc_ctx *ipc_accept(struct ipc_ctx *ctx)                                          __attribute__((nonnull(1)));

bool ipc_recv_timeout(struct ipc_ctx *ctx, int secs) __attribute__((nonnull(1)));

bool ipc_event_loop(struct ipc_ctx *ctx)              __attribute__((nonnull(1)));
ssize_t ipc_send_string(int fd, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

bool ipc_send_message(struct ipc_ctx *ctx, char **pairs, int num_pairs, struct ipc_pair *result) __attribute__((nonnull(1, 2)));
bool ipc_send_message2(struct ipc_ctx *ctx, struct ipc_pair *data, struct ipc_pair *resp);

struct ipc_token {
	char *cmd, *id, *status, *arg;
	char *data;
	bool (*handler)(struct ipc_ctx *, struct ipc_token *);
};

int ipc_parse_token(char *token, struct ipc_token *tok) __attribute__((nonnull(1, 2)));
void ipc_free_token(struct ipc_token *tok)              __attribute__((nonnull(1)));

ssize_t ipc_recv_data(int fd, char *buf, size_t sz)                __attribute__((nonnull(2)));
ssize_t ipc_recv_token(struct ipc_ctx *ctx, struct ipc_token *tok) __attribute__((nonnull(1, 2)));

#endif /* _IPC_H_ */
