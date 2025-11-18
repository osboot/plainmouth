// SPDX-License-Identifier: GPL-2.0-or-later
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include <curses.h>
#include <panel.h>

#include "helpers.h"
#include "request.h"
#include "widget.h"
#include "plugin.h"

struct askpass {
	struct input *input;

	int text_nlines;
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

	wchar_t *text = req_get_wchars(req, "text");
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int nlines = req_get_int(req, "height", -1);
	int ncols = req_get_int(req, "width", -1);
	bool borders = widget_borders(req, bdr);

	int txt_nlines, txt_ncols;

	text_size(text, &txt_nlines, &txt_ncols);

	nlines = MAX(nlines, txt_nlines + 1);
	ncols  = MAX(ncols, txt_ncols);

	askpass->text_nlines = txt_nlines;
	askpass->borders = borders;

	if (borders) {
		ncols  += 2;
		nlines += 2;
	}

	position_center(ncols, nlines, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);
	wbkgd(win, COLOR_PAIR(COLOR_PAIR_WINDOW));

	if (borders) {
		wborder(win,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);
	}

	begin_y = begin_x = (borders ? 1 : 0);

	if (text) {
		write_mvwtext(win, begin_y, begin_x, text);
		free(text);
	}
	wmove(win, 0, 0);

	askpass->input = input_new(win, begin_y + askpass->text_nlines, begin_x,
			(borders ? (ncols - 2) : ncols));
	askpass->input->force_chr = L'*';

	panel = new_panel(win);
	set_panel_userptr(panel, askpass);

	return panel;
}

static enum p_retcode p_askpass_delete(PANEL *panel)
{
	struct askpass *askpass = (struct askpass *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	input_free(askpass->input);

	del_panel(panel);
	delwin(win);

	free(askpass);

	return P_RET_OK;
}

static enum p_retcode p_askpass_input(PANEL *panel, wchar_t code)
{
	struct askpass *askpass = (struct askpass *) panel_userptr(panel);

	return input_wchar(askpass->input, code)
		? P_RET_OK : P_RET_ERR;
}

static enum p_retcode p_askpass_get_cursor(PANEL *panel, int *y, int *x)
{
	const struct askpass *askpass = panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	return get_abs_cursor(win, askpass->input->win, y, x)
		? P_RET_OK : P_RET_ERR;
}

static enum p_retcode p_askpass_result(struct request *req, PANEL *panel)
{
	const struct askpass *askpass = panel_userptr(panel);

	ipc_send_string(req_fd(req), "RESPDATA %s PASSWORD=%ls", req_id(req), askpass->input->data ?: L"");
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_0=%d", req_id(req), askpass->input->finished);

	return P_RET_OK;
}

static bool p_askpass_finished(PANEL *panel)
{
	const struct askpass *askpass = panel_userptr(panel);

	return askpass->input->finished;
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
