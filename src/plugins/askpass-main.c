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
	wchar_t *password;
	int passcap;
	int passlen;

	int cursor_x;
	int cursor_y;

	int label_nlines;
	bool borders;
	bool enter;
};

static PANEL *p_askpass_create(struct request *req)
{
	PANEL *panel;
	chtype bdr[BORDER_SIZE];
	struct askpass *askpass;

	askpass = calloc(1, sizeof(*askpass));
	if (!askpass) {
		warn("calloc askpass");
		return NULL;
	}

	wchar_t *label = req_get_wchars(req, "label");
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int nlines = req_get_int(req, "height", -1);
	int ncols = req_get_int(req, "width", -1);
	bool borders = widget_borders(req, bdr);

	int lbl_nlines, lbl_maxwidth;

	widget_text_lines(label, &lbl_nlines, &lbl_maxwidth);

	nlines = MAX(nlines, lbl_nlines + 1);
	ncols  = MAX(ncols, lbl_maxwidth);

	askpass->label_nlines = lbl_nlines;
	askpass->borders = borders;

	if (borders) {
		ncols  += 2;
		nlines += 2;
	}

	widget_begin_yx(ncols, borders, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);
	wbkgd(win, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (borders) {
		wborder(win,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);
	}

	begin_y = begin_x = 0;

	if (borders)
		begin_y = begin_x = 1;

	if (label) {
		widget_mvwtext(win, begin_y, begin_x, label);
		free(label);
	}

	askpass->cursor_x = begin_x;
	askpass->cursor_y = begin_y + askpass->label_nlines;

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

	if (askpass->borders) {
		begin_x += 1;
		begin_y += 1;
		max_x -= 2;
	}

	begin_y += askpass->label_nlines;

	for (; i < width; i++)
		mvwprintw(win, begin_y, begin_x + i, "*");

	askpass->cursor_x = begin_x + i;
	askpass->cursor_y = begin_y;

	for (; i < max_x; i++)
		mvwprintw(win, begin_y, begin_x + i, " ");

	return P_RET_OK;
}

static enum p_retcode p_askpass_get_cursor(PANEL *panel, int *y, int *x)
{
	const struct askpass *askpass = panel_userptr(panel);

	*x = askpass->cursor_x;
	*y = askpass->cursor_y;

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
	.p_create_instance = p_askpass_create,
	.p_delete_instance = p_askpass_delete,
	.p_update_instance = NULL,
	.p_input           = p_askpass_input,
	.p_finished        = p_askpass_finished,
	.p_result          = p_askpass_result,
	.p_get_cursor      = p_askpass_get_cursor,
};
