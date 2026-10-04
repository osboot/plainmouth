// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/stat.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <err.h>

#include "macros.h"
#include "plugin.h"
#include "widget.h"

#define TAIL_BUFFER_SIZE 65536

struct tailbox {
	int file_fd;
	struct pollfd timer;
	off_t offset;
	size_t length;
	char buffer[TAIL_BUFFER_SIZE];
	struct widget *view;
	struct widget *button;
	bool failed;
};

static bool tailbox_text(struct tailbox *st)
{
	wchar_t *text = calloc(st->length + 1, sizeof(*text));
	if (!text)
		return false;
	mbstate_t state = { 0 };
	size_t pos = 0, n = 0;
	while (pos < st->length) {
		wchar_t ch;
		size_t size = mbrtowc(&ch, st->buffer + pos, st->length - pos, &state);
		if (size == (size_t) -2)
			break;
		if (size == (size_t) -1 || !size) {
			memset(&state, 0, sizeof(state));
			size = 1;
			ch = L'?';
		}
		text[n++] = ch;
		pos += size;
	}
	bool ok = widget_set(st->view, PROP_TEXT_VALUE, text);
	free(text);
	return ok;
}

static enum p_event_result tailbox_read(struct tailbox *st)
{
	struct stat sb;
	if (fstat(st->file_fd, &sb) < 0)
		return P_EVENT_ERROR;
	bool changed = false;
	if (sb.st_size < st->offset) {
		st->offset = 0;
		st->length = 0;
		changed = true;
	}
	char chunk[TAIL_BUFFER_SIZE];
	ssize_t size = pread(st->file_fd, chunk, sizeof(chunk), st->offset);
	if (size < 0) {
		if (errno == EINTR || errno == EAGAIN)
			return P_EVENT_IDLE;
		return P_EVENT_ERROR;
	}
	if (size > 0) {
		size_t n = (size_t) size;
		if (st->length + n > sizeof(st->buffer)) {
			size_t drop = st->length + n - sizeof(st->buffer);
			memmove(st->buffer, st->buffer + drop, st->length - drop);
			st->length -= drop;
		}
		memcpy(st->buffer + st->length, chunk, n);
		st->length += n;
		st->offset += size;
		changed = true;
	}
	if (!changed)
		return P_EVENT_IDLE;
	return tailbox_text(st) ? P_EVENT_REDRAW : P_EVENT_ERROR;
}

static enum p_retcode tailbox_delete(struct widget *root)
{
	struct tailbox *st = root->data;
	if (st) {
		if (st->file_fd >= 0)
			close(st->file_fd);
		if (st->timer.fd >= 0)
			close(st->timer.fd);
		free(st);
		root->data = NULL;
	}
	return P_RET_OK;
}

static struct widget *tailbox_create(struct request *req)
{
	int width, height;
	const char *path = req_get_val(req, "file");
	if (!path || !*path) {
		req_error(req, "field is missing: file");
		return NULL;
	}
	if (!req_read_int(req, "width", &width) || !req_read_int(req, "height", &height))
		return NULL;
	if (width < 3 || height < 4) {
		req_error(req, "invalid tailbox dimensions");
		return NULL;
	}
	struct widget *root = make_window();
	struct tailbox *st = calloc(1, sizeof(*st));
	if (!root || !st) {
		widget_free(root);
		free(st);
		return NULL;
	}
	root->style_owner = req->r_style_owner;
	root->data = st;
	st->file_fd = st->timer.fd = -1;
	st->file_fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	struct stat sb;
	if (st->file_fd < 0 || fstat(st->file_fd, &sb) < 0) {
		req_error(req, "unable to open file: %s", path);
		goto fail;
	}
	if (!S_ISREG(sb.st_mode)) {
		req_error(req, "tailbox requires a regular file");
		goto fail;
	}
	st->offset = sb.st_size > TAIL_BUFFER_SIZE ? sb.st_size - TAIL_BUFFER_SIZE : 0;
	struct widget *parent = root;
	if (req_get_bool(req, "border", false)) {
		parent = make_border_vbox(root);
		if (!parent)
			goto fail;
	}
	st->view = make_tailview();
	if (!st->view)
		goto fail;
	widget_add(parent, st->view);
	st->button = make_button(L"OK");
	if (!st->button)
		goto fail;
	st->button->w_id = 1;
	widget_add(parent, st->button);
	if (tailbox_read(st) == P_EVENT_ERROR)
		goto fail;
	st->timer.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	st->timer.events = POLLIN;
	struct itimerspec interval = {
		.it_interval = { .tv_nsec = 100000000 },
		.it_value = { .tv_nsec = 100000000 },
	};
	if (st->timer.fd < 0 || timerfd_settime(st->timer.fd, 0, &interval, NULL) < 0) {
		req_error(req, "unable to start tailbox timer");
		goto fail;
	}
	int x = req_get_int(req, "x", -1), y = req_get_int(req, "y", -1);
	position_center(width, height, &y, &x);
	widget_measure_tree(root);
	widget_layout_tree(root, x, y, width, height);
	widget_render_tree(root);
	return root;
fail:
	tailbox_delete(root);
	widget_free(root);
	return NULL;
}

static size_t tailbox_pollfds(struct widget *root, const struct pollfd **fds)
{
	struct tailbox *st = root->data;
	*fds = &st->timer;
	return 1;
}

static enum p_event_result tailbox_event(struct widget *root, const struct pollfd *fd)
{
	struct tailbox *st = root->data;
	uint64_t ticks;
	if (fd->revents & (POLLERR | POLLHUP | POLLNVAL))
		goto fail;
	ssize_t size = read(fd->fd, &ticks, sizeof(ticks));
	if (size < 0 && (errno == EAGAIN || errno == EINTR))
		return P_EVENT_IDLE;
	if (size != sizeof(ticks))
		goto fail;
	enum p_event_result result = tailbox_read(st);
	if (result != P_EVENT_ERROR)
		return result;
fail:
	st->failed = true;
	return P_EVENT_ERROR;
}

static bool tailbox_finished(struct widget *root)
{
	struct tailbox *st = root->data;
	bool clicked = false;
	widget_get(st->button, PROP_BUTTON_STATE, &clicked);
	return st->failed || clicked;
}

static enum p_retcode tailbox_result(struct request *req, struct widget *root)
{
	struct tailbox *st = root->data;
	if (st->failed) {
		req_error(req, "tailbox file read failed");
		return P_RET_ERR;
	}
	bool clicked = false;
	widget_get(st->button, PROP_BUTTON_STATE, &clicked);
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_1=%d", req_id(req), clicked);
	return P_RET_OK;
}

static enum p_retcode tailbox_set_value(struct request *req, struct widget *root)
{
	struct tailbox *st = root->data;
	int button;
	bool clicked;
	if (!req_read_int(req, "button", &button) ||
	    !req_read_bool(req, "clicked", true, &clicked))
		return P_RET_ERR;
	if (button != 1) {
		req_error(req, "widget not found: button=%d", button);
		return P_RET_ERR;
	}
	return widget_set(st->button, PROP_BUTTON_STATE, &clicked) ? P_RET_OK : P_RET_ERR;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "tailbox",
	.desc = "Follow the end of a regular file.",
	.p_create_instance = tailbox_create,
	.p_delete_instance = tailbox_delete,
	.p_set_value_instance = tailbox_set_value,
	.p_finished = tailbox_finished,
	.p_result = tailbox_result,
	.p_pollfds = tailbox_pollfds,
	.p_handle_event = tailbox_event,
};
