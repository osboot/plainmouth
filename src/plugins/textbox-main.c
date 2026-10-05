// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "macros.h"
#include "plugin.h"
#include "plugin_helpers.h"
#include "widget.h"

#define TEXTBOX_BYTES 65536
#define TEXTBOX_CELLS 1048576

static wchar_t *textbox_read(struct request *req, const char *path)
{
	struct stat sb;
	wchar_t *text = NULL;
	char *buffer = NULL;
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);

	if (fd < 0 || fstat(fd, &sb) < 0) {
		req_error(req, "unable to open file: %s", path);
		goto out;
	}

	if (!S_ISREG(sb.st_mode) || sb.st_size > TEXTBOX_BYTES) {
		req_error(req, "textbox requires a regular file of at most 65536 bytes");
		goto out;
	}

	buffer = malloc(TEXTBOX_BYTES + 1);
	if (!buffer)
		goto out;

	size_t length = 0;

	while (length <= TEXTBOX_BYTES) {
		ssize_t n = read(fd, buffer + length, TEXTBOX_BYTES + 1 - length);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			req_error(req, "textbox file read failed: %s", path);
			goto out;
		}
		if (!n)
			break;
		length += (size_t) n;
	}

	if (length > TEXTBOX_BYTES) {
		req_error(req, "textbox file exceeds 65536 bytes");
		goto out;
	}

	text = calloc(length * 8 + 1, sizeof(*text));
	if (!text)
		goto out;

	mbstate_t state = { 0 };
	size_t pos = 0, used = 0;
	int col = 0;

	while (pos < length) {
		wchar_t ch;
		size_t n = mbrtowc(&ch, buffer + pos, length - pos, &state);
		if (n == (size_t) -1 || n == (size_t) -2 || !n) {
			memset(&state, 0, sizeof(state));
			n = 1;
			ch = L'?';
		}
		pos += n;
		if (ch == L'\t') {
			int spaces = 8 - col % 8;
			while (spaces--) {
				text[used++] = L' ';
				col++;
			}
			continue;
		}
		int width = wcwidth(ch);
		if (ch == L'\n')
			col = 0;
		else if (width < 0) {
			ch = L'?';
			col++;
		} else
			col += width;
		text[used++] = ch;
	}
out:
	if (fd >= 0)
		close(fd);
	free(buffer);
	return text;
}

static struct widget *textbox_create(struct request *req)
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
		req_error(req, "invalid textbox dimensions");
		return NULL;
	}

	wchar_t *text = textbox_read(req, path);

	if (!text)
		return NULL;

	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	struct widget *view = make_textview(text);
	free(text);

	if (!root || !view) {
		widget_free(root);
		widget_free(view);
		return NULL;
	}

	widget_add(parent, view);

	struct widget *button = plugin_create_close_button(req);
	if (!button)
		goto fail;

	widget_add(parent, button);
	widget_measure_tree(root);

	/* The pad is rectangular even when the source lines are ragged. */
	if ((size_t) view->pref_w * (size_t) view->pref_h > TEXTBOX_CELLS) {
		req_error(req, "textbox content exceeds 1048576 screen cells");
		goto fail;
	}

	int x = req_get_int(req, "x", -1);
	int y = req_get_int(req, "y", -1);

	position_center(width, height, &y, &x);

	widget_layout_tree(root, x, y, width, height);
	widget_render_tree(root);
	return root;
fail:
	widget_free(root);
	return NULL;
}

static struct widget *textbox_button(struct widget *root)
{
	return find_widget_by_type_and_id(root, WIDGET_BUTTON, 1);
}

static enum p_retcode textbox_set_value(struct request *req, struct widget *root)
{
	if (req_get_val(req, "button")) {
		int id;
		bool clicked;

		if (!req_read_int(req, "button", &id) ||
		    !req_read_bool(req, "clicked", true, &clicked))
			return P_RET_ERR;

		if (id != 1) {
			req_error(req, "widget not found: button=%d", id);
			return P_RET_ERR;
		}

		return widget_set(textbox_button(root), PROP_BUTTON_STATE, &clicked) ? P_RET_OK : P_RET_ERR;
	}

	bool has_x = req_get_val(req, "scroll-x") != NULL;
	bool has_y = req_get_val(req, "scroll-y") != NULL;
	int x = 0, y = 0;

	if ((!has_x && !has_y) || (has_x && !req_read_int(req, "scroll-x", &x)) ||
	    (has_y && !req_read_int(req, "scroll-y", &y)) || x < 0 || y < 0) {
		req_error(req, "nonnegative scroll-x or scroll-y is required");
		return P_RET_ERR;
	}

	struct widget *pad = find_widget_by_type_and_id(root, WIDGET_PAD_BOX, 0);
	if (!pad)
		return P_RET_ERR;

	if (has_x && !widget_set(pad, PROP_SCROLL_X, &x))
		return P_RET_ERR;

	if (has_y && !widget_set(pad, PROP_SCROLL_Y, &y))
		return P_RET_ERR;

	return P_RET_OK;
}

static bool textbox_finished(struct widget *root)
{
	bool clicked = false;
	widget_get(textbox_button(root), PROP_BUTTON_STATE, &clicked);
	return clicked;
}

static enum p_retcode textbox_result(struct request *req, struct widget *root)
{
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_1=%d", req_id(req), textbox_finished(root));
	return P_RET_OK;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "textbox",
	.desc = "Display a snapshot of a regular file.",
	.p_create_instance = textbox_create,
	.p_set_value_instance = textbox_set_value,
	.p_finished = textbox_finished,
	.p_result = textbox_result,
};
