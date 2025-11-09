// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <ncurses.h>
#include <panel.h>

#include "helpers.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

struct askpass {
	char *label;

	wchar_t *password;
	int passcap;
	int passlen;

	bool border;
	bool enter;
};

static PANEL *p_askpass_create(struct request *req _UNUSED)
{
	PANEL *panel;
	struct askpass *askpass;

	askpass = calloc(1, sizeof(*askpass));
	if (!askpass) {
		warn("calloc askpass");
		return NULL;
	}

	const char *label = ipc_get_val(req_data(req), "label");
	bool border = ipc_get_bool(req_data(req), "border", false);
	int begin_x = ipc_get_int(req_data(req), "x", -1);
	int begin_y = ipc_get_int(req_data(req), "y", -1);
	int ncols = ipc_get_int(req_data(req), "width", -1);

	int border_cols = (border ? 2 : 0);
	int nlines  = 1 + border_cols + (label ? 1 : 0);

	ncols = (ncols > 0 ? ncols : COLS - 4) + border_cols;

	widget_begin_yx(ncols, askpass->border, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);

	if (border)
		box(win, 0, 0);

	if (label) {
		begin_y = begin_x = 0;

		if (border)
			begin_y = begin_x = 1;

		askpass->label = strdup(label);
		mvwprintw(win, begin_y, begin_x, "%s", label);
	}

	askpass->border = border;

	panel = new_panel(win);
	set_panel_userptr(panel, askpass);

	return panel;
}

static enum p_retcode p_askpass_delete(PANEL *panel)
{
	struct askpass *askpass = (struct askpass *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	del_panel(panel);
	delwin(win);

	if (askpass->label)
		free(askpass->label);

	if (askpass->password)
		free(askpass->password);

	free(askpass);

	return P_RET_OK;
}

static bool pass_unchr(struct askpass *p)
{
	if (p->passlen > 0)
		p->password[--p->passlen] = '\0';
	return true;
}

static bool pass_chr(struct askpass *p, wchar_t key)
{
	int len = p->passlen + 2;

	if (p->passcap < len) {
		wchar_t *data = realloc(p->password, (size_t) len * sizeof(wchar_t));

		if (!data) {
			warn("realloc");
			return false;
		}
		p->password = data;
		p->passcap = len;
	}

	p->password[p->passlen++] = key;
	p->password[p->passlen] = '\0';

	return true;
}

static enum p_retcode p_askpass_input(PANEL *panel, wchar_t code)
{
	struct askpass *askpass = (struct askpass *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	if (askpass->enter)
		return P_RET_OK;

	switch (code) {
		case KEY_ENTER:
		case L'\n':
			warnx("askpass password [%ls]", askpass->password);
			askpass->enter = true;
			break;

		case KEY_BACKSPACE:
		case L'\b':
		case 127:
			warnx("askpass backspace [%04x]", code);
			pass_unchr(askpass);
			break;

		default:
			warnx("askpass input [%lc]", code);
			pass_chr(askpass, code);
	}

	int max_x = getmaxx(win);
	int width = MIN(askpass->passlen, max_x);
	int i = 0, begin_x = 0, begin_y = 0;

	if (askpass->border) {
		begin_x += 1;
		begin_y += 1;
		max_x -= 2;
	}

	if (askpass->label) {
		begin_y += 1;
	}

	for (; i < width; i++)
		mvwprintw(win, begin_y, begin_x + i, "*");

	for (; i < max_x; i++)
		mvwprintw(win, begin_y, begin_x + i, " ");

	return P_RET_OK;
}

static enum p_retcode p_askpass_result(struct request *req, PANEL *panel)
{
	const struct askpass *askpass = panel_userptr(panel);

	ipc_send_string(req_fd(req), "RESPDATA %s PASSWORD=%ls", req_id(req), askpass->password ?: L"");
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_OK=%d", req_id(req), askpass->enter);

	return P_RET_OK;
}

static bool p_askpass_finished(PANEL *panel)
{
	const struct askpass *askpass = panel_userptr(panel);

	return askpass->enter;
}

struct plugin plugin = {
	.name              = "askpass",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_widget   = p_askpass_create,
	.p_delete_widget   = p_askpass_delete,
	.p_update_widget   = NULL,
	.p_input           = p_askpass_input,
	.p_finished        = p_askpass_finished,
	.p_result          = p_askpass_result,
};
