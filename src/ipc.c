// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <err.h>

#include "helpers.h"
#include "ipc.h"

/*
 * C: HELLO
 * S: TAKE <ID>
 * C: PAIR <ID> <KEY>=<VALUE>
 * C: DONE <ID>
 * S: RESPDATA <ID> <KEY>=<VALUE>
 * S: RESPONSE <ID> <STATUS> <MESSAGE>
 * C: PING
 * S: PONG
 */

#define FIELD_DELIM " \t"

static void sanitize_newlines(char *s) __attribute__((nonnull(1)));

static ssize_t sendmsg_retry(int fd, const struct msghdr *msg, int flags) __attribute__((nonnull(2)));
static ssize_t recvmsg_retry(int fd, struct msghdr *msg, int flags)       __attribute__((nonnull(2)));

static ssize_t send_line(int fd, char *line) __attribute__((nonnull(2)));

static bool handle_hllo(struct ipc_ctx *, struct ipc_token *) __attribute__((nonnull(1, 2)));
static bool handle_take(struct ipc_ctx *, struct ipc_token *) __attribute__((nonnull(1, 2)));
static bool handle_pair(struct ipc_ctx *, struct ipc_token *) __attribute__((nonnull(1, 2)));
static bool handle_done(struct ipc_ctx *, struct ipc_token *) __attribute__((nonnull(1, 2)));
static bool handle_dummy(struct ipc_ctx *, struct ipc_token *) __attribute__((nonnull(1, 2)));

struct cmd_handler {
	const char *name;
	bool (*fn)(struct ipc_ctx *, struct ipc_token *);
};

static struct cmd_handler handlers[] = {
	{ "HELLO",    handle_hllo  },
	{ "TAKE",     handle_take  },
	{ "PAIR",     handle_pair  },
	{ "DONE",     handle_done  },
	{ "RESPDATA", handle_dummy },
	{ "RESPONSE", handle_dummy },
	{ NULL,       NULL         }
};

void sanitize_newlines(char *s)
{
	for (; *s; ++s)
		if (*s == '\n' || *s == '\r')
			*s = ' ';
}

void ipc_buffer_free(struct ipc_buffer *a)
{
	if (a->data)
		free(a->data);
	a->len = 0;
}

void ipc_buffer_append(struct ipc_buffer *a, const char *buf, size_t n)
{
	void *data = realloc(a->data, a->len + n);
	if (!data) {
		warn("realloc failed");
		return;
	}
	a->data = data;
	memcpy(a->data + a->len, buf, n);
	a->len += n;
}

char *ipc_buffer_next_line(struct ipc_buffer *a)
{
	if (!a->data || a->len == 0)
		return NULL;

	char *p = memchr(a->data, '\0', a->len);
	if (!p)
		return NULL;

	size_t toklen = (size_t) (p - a->data);
	char *tok = strndup(a->data, toklen);

	a->len -= (toklen + 1);

	if (a->len > 0) {
		memmove(a->data, p + 1, a->len);

		void *data = realloc(a->data, a->len);
		if (data)
			a->data = data;
	} else {
		free(a->data);
		a->data = NULL;
	}

	return tok;
}

ssize_t sendmsg_retry(int fd, const struct msghdr *msg, int flags)
{
	return TEMP_FAILURE_RETRY(sendmsg(fd, msg, flags));
}

ssize_t recvmsg_retry(int fd, struct msghdr *msg, int flags)
{
	return TEMP_FAILURE_RETRY(recvmsg(fd, msg, flags));
}

ssize_t send_line(int fd, char *line)
{
	struct iovec iov = {
		.iov_base = line,
		.iov_len = strlen(line) + 1,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	ssize_t size = -1;

	if (IS_DEBUG())
		warnx("pid=%-10d SEND: %s", getpid(), line);

	if (fd >= 0) {
		size = sendmsg_retry(fd, &msg, 0);
		if (size < 0)
			warn("sendmsg");
	}

	return size;
}

ssize_t ipc_recv_data(int fd, char *buf, size_t sz)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = sz,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	ssize_t size = -1;

	buf[0] = '\0';

	if (fd >= 0) {
		size = recvmsg_retry(fd, &msg, 0);
		if (size < 0)
			warn("recvmsg");
	}

	if (IS_DEBUG())
		warnx("pid=%-10d RECV: %s", getpid(), buf);

	return size;
}

ssize_t ipc_send_string(int fd, const char *fmt, ...)
{
	char *text;
	va_list ap;

	if (streq(fmt, "%s")) {
		va_start(ap, fmt);
		text = va_arg(ap, char *);
		va_end(ap);

		return send_line(fd, text);
	}

	ssize_t size;

	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (size <= 0)
		return 0;
	size += 1;

	text = malloc((size_t) size);
	if (!text)
		return -1;

	va_start(ap, fmt);
	size = vsnprintf(text, (size_t) size, fmt, ap);
	va_end(ap);

	if (size < 0) {
		free(text);
		return -1;
	}

	size = send_line(fd, text);
	free(text);

	return size;
}

void ipc_pair_free(struct ipc_pair *pair)
{
	for (size_t i = 0; i < pair->num_kv; i++) {
		free(pair->kv[i].key);
		free(pair->kv[i].val);
	}
	free(pair->kv);
}

void ipc_msg_free(struct ipc_message *m)
{
	ipc_pair_free(&m->data);
	ipc_pair_free(&m->resp);
	free(m->id);
	free(m);
}

struct ipc_message *ipc_msg_find(struct ipc_ctx *ctx, const char *id)
{
	struct ipc_message *m;

	LIST_FOREACH(m, &ctx->msgs, entries) {
		if (streq(m->id, id))
			return m;
	}
	return NULL;
}

struct ipc_message *ipc_msg_add(struct ipc_ctx *ctx, const char *id)
{
	struct ipc_message *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;

	m->id = strdup(id);
	if (!m->id) {
		free(m);
		return NULL;
	}

	LIST_INSERT_HEAD(&ctx->msgs, m, entries);

	return m;
}

static bool inc_pair_capacity(struct ipc_pair *p)
{
	if (p->num_kv >= p->capacity) {
		size_t newcap = (p->capacity > 0 ? p->capacity * 2 : 1);

		void *kv = realloc(p->kv, newcap * sizeof(struct ipc_kv));
		if (!kv) {
			warn("realloc failed");
			return false;
		}

		p->kv = kv;
		p->capacity = newcap;
	}
	return true;
}

bool ipc_pair_add(struct ipc_pair *pairs, const char *key, const char *val)
{
	if (!inc_pair_capacity(pairs))
		return false;

	pairs->kv[pairs->num_kv].key = strdup(key);
	pairs->kv[pairs->num_kv].val = strdup(val);
	pairs->num_kv++;

	return true;
}

bool ipc_pair_sprintf(struct ipc_pair *pairs, const char *key, const char *fmt, ...)
{
	if (!inc_pair_capacity(pairs))
		return false;

	ssize_t size;
	va_list ap;

	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (size <= 0)
		return false;
	size += 1;

	char *val = malloc((size_t) size);
	if (!val)
		return false;

	va_start(ap, fmt);
	size = vsnprintf(val, (size_t) size, fmt, ap);
	va_end(ap);

	if (size < 0) {
		free(val);
		return false;
	}

	pairs->kv[pairs->num_kv].key = strdup(key);
	pairs->kv[pairs->num_kv].val = val;
	pairs->num_kv++;

	return true;
}

bool handle_hllo(struct ipc_ctx *ctx,
		struct ipc_token *tok __attribute__((unused)))
{
	char idbuf[32];
	snprintf(idbuf, sizeof(idbuf), "%lu", ctx->next_msgid);

	if (ipc_send_string(ctx->fd, "TAKE %s", idbuf) < 0)
		return false;

	ipc_msg_add(ctx, idbuf);
	ctx->next_msgid++;

	return true;
}

bool handle_take(struct ipc_ctx *ctx, struct ipc_token *tok)
{
	ipc_msg_add(ctx, tok->id);
	return true;
}

bool handle_pair(struct ipc_ctx *ctx, struct ipc_token *tok)
{
	char *eq = strchr(tok->arg, '=');
	if (!eq) {
		ipc_send_string(ctx->fd, "RESPONSE %s ERROR 'PAIR' bad format", tok->id);
		return false;
	}

	*eq = '\0';

	char *key = tok->arg;
	char *val = eq + 1;

	sanitize_newlines(key);
	sanitize_newlines(val);

	struct ipc_message *msg = ipc_msg_find(ctx, tok->id);
	if (!msg)
		msg = ipc_msg_add(ctx, tok->id);

	return ipc_pair_add(&msg->data, key, val);
}

bool handle_done(struct ipc_ctx *ctx, struct ipc_token *tok)
{
	struct ipc_message *msg = ipc_msg_find(ctx, tok->id);
	if (!msg) {
		ipc_send_string(ctx->fd, "RESPONSE 0 ERROR 'DONE' got unknown id '%s'", tok->id);
		return false;
	}

	int res = 0;

	if (ctx->handle_message)
		res = ctx->handle_message(ctx, msg, ctx->data);

	LIST_REMOVE(msg, entries);
	ipc_msg_free(msg);

	return (!res)
		? ipc_send_string(ctx->fd, "RESPONSE %s OK", tok->id) > 0
		: ipc_send_string(ctx->fd, "RESPONSE %s ERROR", tok->id) > 0;
}

bool handle_dummy(struct ipc_ctx *ctx __attribute__((unused)),
		 struct ipc_token *tok)
{
	if (IS_DEBUG())
		warnx("Got %s id=%s", tok->cmd, tok->id);
	return true;
}

bool ipc_event_loop(struct ipc_ctx *ctx)
{
	struct pollfd pfd = {
		.fd = ctx->fd,
		.events = POLLIN,
	};

	while (1) {
		int r;

		if (ctx->event_loop_iter && !ctx->event_loop_iter(ctx->data))
			break;

		errno = 0;
		if ((r = poll(&pfd, 1, 3000)) < 0) {
			if (errno == EINTR)
				continue;
			warn("poll");
			return false;
		}

		if (!r)
			continue;

		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			close(ctx->fd);
			ctx->fd = -1;
			break;
		}

		if (pfd.revents & POLLIN) {
			char buf[BUFSIZ];

			ssize_t len = ipc_recv_data(ctx->fd, buf, sizeof(buf));
			if (len <= 0)
				break;

			ipc_buffer_append(&ctx->inbuf, buf, (size_t) len);

			char *s;
			while ((s = ipc_buffer_next_line(&ctx->inbuf)) != NULL) {
				struct ipc_token tok;
				int ret = ipc_parse_token(s, &tok);

				if (ret < 0) {
					if (ret == -ESRCH) {
						ipc_send_string(ctx->fd, "RESPONSE %s ERROR unknown command '%s'",
								tok.cmd,
								tok.id ? tok.id : "0");
					} else {
						ipc_send_string(ctx->fd, "RESPONSE %s ERROR bad format",
								tok.id ? tok.id : "0");
					}
				} else if (!tok.handler(ctx, &tok)) {
					warnx("command processing failed");
				}
				free(s);
			}
		}
	}

	return true;
}

void ipc_init(struct ipc_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->fd = -1;

	LIST_INIT(&ctx->msgs);
}

void ipc_free(struct ipc_ctx *ctx)
{
	if (ctx->fd >= 0)
		close(ctx->fd);

	struct ipc_message *m1, *m2;

	m1 = LIST_FIRST(&ctx->msgs);
	while (m1) {
		m2 = LIST_NEXT(m1, entries);
		ipc_msg_free(m1);
		m1 = m2;
	}

	ipc_buffer_free(&ctx->inbuf);
}

void ipc_free_token(struct ipc_token *tok)
{
	free(tok->data);
	memset(tok, 0, sizeof(*tok));
}

int ipc_parse_token(char *token, struct ipc_token *tok)
{
	char *sv = NULL;

	memset(tok, 0, sizeof(*tok));

	if ((tok->cmd = strtok_r(token, FIELD_DELIM, &sv)) == NULL) {
		warnx("ERROR: missing 'command' field");
		return -EINVAL;
	}

	for (struct cmd_handler *handler = handlers; handler->name; handler++) {
		if (streq(tok->cmd, handler->name)) {
			tok->handler = handler->fn;
			break;
		}
	}

	if (!tok->handler) {
		warnx("ERROR unknown command '%s'", tok->cmd);
		return -ESRCH;
	}

	tok->data = token;

	if (streq(tok->cmd, "HELLO") || streq(tok->cmd, "PING") || streq(tok->cmd, "PONG"))
		return 0;

	if ((tok->id = strtok_r(NULL, FIELD_DELIM, &sv)) == NULL) {
		warnx("ERROR: '%s' missing 'id' field", tok->cmd);
		return -EINVAL;
	}

	if (streq(tok->cmd, "PAIR") || streq(tok->cmd, "RESPDATA")) {
		if ((tok->arg = strtok_r(NULL, "", &sv)) == NULL) {
			warnx("ERROR: '%s' missing 'key=value'", tok->cmd);
			return -EINVAL;
		}
	} else if (streq(tok->cmd, "RESPONSE")) {
		if ((tok->status = strtok_r(NULL, FIELD_DELIM, &sv)) == NULL) {
			warnx("ERROR: '%s' missing 'status' field", tok->cmd);
			return -EINVAL;
		}
		tok->arg = strtok_r(NULL, "", &sv);
	}

	return 0;
}

ssize_t ipc_recv_token(struct ipc_ctx *ctx, struct ipc_token *tok)
{
	while (1) {
		char tmp[BUFSIZ] = { 0 };

		char *line = ipc_buffer_next_line(&ctx->inbuf);
		if (line) {
			ssize_t size = (ssize_t) strlen(line) + 1;
			return !ipc_parse_token(line, tok) ? size : -1;
		}

		ssize_t n = ipc_recv_data(ctx->fd, tmp, sizeof(tmp));
		if (n <= 0)
			return -1;

		ipc_buffer_append(&ctx->inbuf, tmp, (size_t) n);
	}
	return 0;
}

bool ipc_send_message(struct ipc_ctx *ctx, char **pairs, int num_pairs,
		struct ipc_pair *result)
{
	struct ipc_token response1 = { 0 };
	struct ipc_token response2 = { 0 };
	struct ipc_pair resp = { 0 };
	ssize_t size = 0;
	bool ret = false;

	if (ipc_send_string(ctx->fd, "%s", "HELLO") < 0)
		goto finish;

	if ((size = ipc_recv_token(ctx, &response1)) <= 0)
		goto finish;

	if (!streq(response1.cmd, "TAKE")) {
		warnx("ERROR: unexpected answer: %s", response1.cmd);
		goto finish;
	}

	for (int i = 0; i < num_pairs; i++) {
		if (pairs[i] && pairs[i][0] != '\0' &&
		    ipc_send_string(ctx->fd, "PAIR %s %s", response1.id, pairs[i]) < 0)
			goto finish;
	}

	if (ipc_send_string(ctx->fd, "DONE %s", response1.id) < 0)
		goto finish;

	while (1) {
		if ((size = ipc_recv_token(ctx, &response2)) <= 0)
			goto finish;

		if (streq(response2.cmd, "RESPDATA")) {
			char *eq = strchr(response2.arg, '=');
			if (!eq) {
				warnx("ERROR: bad format of 'RESPDATA'");
				break;
			}

			*eq = '\0';

			char *key = response2.arg;
			char *val = eq + 1;

			ipc_pair_add(&resp, key, val);

			ipc_free_token(&response2);
			continue;
		}

		if (streq(response2.cmd, "RESPONSE")) {
			if (!streq(response2.id, response1.id) &&
			    !streq(response2.id, "0")) {
				warnx("ERROR: command id '%s'. expected '%s'",
						response2.id, response1.id);
				goto finish;
			}

			ret = streq(response2.status, "OK");
			if (!ret && response2.arg)
				warnx("ERROR: %s", response2.arg);
			break;
		}
	}
finish:
	if (result) {
		result->kv = resp.kv;
		result->num_kv = resp.num_kv;
		result->capacity = resp.capacity;
	} else {
		ipc_pair_free(&resp);
	}
	ipc_free_token(&response1);
	ipc_free_token(&response2);

	return ret;
}

bool ipc_send_message2(struct ipc_ctx *ctx, struct ipc_pair *data, struct ipc_pair *resp)
{
	struct ipc_token response1 = { 0 };
	struct ipc_token response2 = { 0 };
	ssize_t size = 0;
	bool ret = false;

	if (ipc_send_string(ctx->fd, "%s", "HELLO") < 0)
		goto finish;

	if ((size = ipc_recv_token(ctx, &response1)) <= 0)
		goto finish;

	if (!streq(response1.cmd, "TAKE")) {
		warnx("ERROR: unexpected answer: %s", response1.cmd);
		goto finish;
	}

	for (size_t i = 0; i < data->num_kv; i++) {
		size = ipc_send_string(ctx->fd, "PAIR %s %s=%s", response1.id,
				data->kv[i].key, data->kv[i].val);
		if (size < 0)
			goto finish;
	}

	if (ipc_send_string(ctx->fd, "DONE %s", response1.id) < 0)
		goto finish;

	while (1) {
		if ((size = ipc_recv_token(ctx, &response2)) <= 0)
			goto finish;

		if (streq(response2.cmd, "RESPDATA")) {
			char *eq = strchr(response2.arg, '=');
			if (!eq) {
				warnx("ERROR: bad format of 'RESPDATA'");
				break;
			}

			if (resp) {
				*eq = '\0';

				char *key = response2.arg;
				char *val = eq + 1;

				ipc_pair_add(resp, key, val);
			}
			continue;
		}

		if (streq(response2.cmd, "RESPONSE")) {
			if (!streq(response2.id, response1.id) &&
			    !streq(response2.id, "0")) {
				warnx("ERROR: command id '%s'. expected '%s'",
						response2.id, response1.id);
				goto finish;
			}

			ret = streq(response2.status, "OK");
			if (!ret && response2.arg)
				warnx("ERROR: %s", response2.arg);
			break;
		}
	}
finish:
	ipc_free_token(&response1);
	ipc_free_token(&response2);

	return ret;
}

bool ipc_recv_timeout(struct ipc_ctx *ctx, int secs)
{
	struct timeval tv = {
		.tv_sec = secs,
	};

	if (setsockopt(ctx->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		warn("setsockopt");
		return false;
	}
	return true;
}

bool ipc_listen(
	struct ipc_ctx *ctx, const char *file_name, int backlog, int sock_flags)
{
	int fd = -1;
	int saved_errno = 0;

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	strlcpy(addr.sun_path, file_name, sizeof(addr.sun_path));

	if (unlink(addr.sun_path) && errno != ENOENT) {
		saved_errno = errno;
		warn("unlink: %s", addr.sun_path);
		goto failure;
	}

	fd = socket(AF_UNIX, SOCK_STREAM | sock_flags, 0);
	if (fd < 0) {
		saved_errno = errno;
		warn("socket: %s", addr.sun_path);
		goto failure;
	}

	if (bind(fd, (struct sockaddr *) &addr, (socklen_t) sizeof(addr))) {
		saved_errno = errno;
		warn("bind: %s", addr.sun_path);
		goto failure;
	}

	if (listen(fd, backlog)) {
		saved_errno = errno;
		warn("listen: %s", addr.sun_path);
		goto failure;
	}

	ctx->fd = fd;
	return true;

failure:
	if (fd >= 0)
		close(fd);

	errno = saved_errno;
	return false;
}

struct ipc_ctx *ipc_accept(struct ipc_ctx *ctx)
{
	struct sockaddr_un sun;
	socklen_t len = sizeof(sun);

	int fd = accept(ctx->fd, (struct sockaddr *) &sun, &len);
	if (fd < 0) {
		warn("accept");
		return NULL;
	}

	struct ipc_ctx *new = calloc(1, sizeof(*new));
	if (!new) {
		warn("calloc ipc_ctx");
		close(fd);
		return NULL;
	}

	new->fd = fd;
	new->data = ctx->data;
	new->handle_message = ctx->handle_message;
	new->event_loop_iter = ctx->event_loop_iter;

	return new;
}

bool ipc_connect(struct ipc_ctx *ctx, const char *file_name, int sock_flags)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | sock_flags, 0);
	if (fd < 0) {
		warn("socket");
		return false;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	strlcpy(addr.sun_path, file_name, sizeof(addr.sun_path));

	if (connect(fd, (const struct sockaddr *) &addr, sizeof(addr))) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
	}

	ctx->fd = fd;
	return true;
}

void ipc_close(struct ipc_ctx *ctx)
{
	if (ctx->fd && ctx->fd >= 0) {
		close(ctx->fd);
		ctx->fd = -1;
	}
}
