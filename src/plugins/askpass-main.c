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

/*
 * Full layout:
 *    +---------------------------+ +---------------------------+
 *    |<long text message>        | |<multi line long           |
 *    |<label text> [= password =]| | text message>             |
 *    +---------------------------+ |<multi line                |
 *                                  | label text> [= password =]|
 *                                  +---------------------------+
 * or without label:
 *    +---------------------------+ +---------------------------+
 *    |<long text message>        | |<multi line long           |
 *    |[= password ==============]| | text message>             |
 *    +---------------------------+ |[= password ==============]|
 *                                  +---------------------------+
 * or without upper text message:
 *    +---------------------------+ +---------------------------+
 *    |<label text> [= password =]| |<multi line                |
 *    +---------------------------+ | label text> [= password =]|
 *                                  +---------------------------+
 * or without any text:
 *    +---------------------------+
 *    |[= password ==============]|
 *    +---------------------------+
 */

#define MIN_INPUT_COLS 10

struct askpass {
	struct message *text;
	struct message *label;
	struct input   *input;
};

static PANEL *p_askpass_create(struct request *req)
{
	PANEL *panel = NULL;
	chtype bdr[BORDER_SIZE];
	struct askpass *askpass;

	askpass = calloc(1, sizeof(*askpass));
	if (!askpass) {
		warn("calloc askpass");
		return NULL;
	}

	wchar_t *text = req_get_wchars(req, "text");
	wchar_t *label = req_get_wchars(req, "label");
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int nlines = req_get_int(req, "height", -1);
	int ncols = req_get_int(req, "width", -1);
	bool borders = widget_borders(req, bdr);

	int lbl_nlines, lbl_ncols;
	int txt_nlines, txt_ncols;

	text_size(label, &lbl_nlines, &lbl_ncols);
	text_size(text, &txt_nlines, &txt_ncols);

	nlines = MAX(nlines, txt_nlines) + (lbl_nlines ?: 1);
	ncols  = MAX(ncols, txt_ncols);

	if (ncols < (lbl_ncols + MIN_INPUT_COLS))
		ncols = lbl_ncols + MIN_INPUT_COLS;

	if (borders) {
		ncols  += 2;
		nlines += 2;
	}

	position_center(ncols, nlines, &begin_y, &begin_x);

	WINDOW *win = newwin(nlines, ncols, begin_y, begin_x);
	if (!win)
		goto fail;
	wbkgd(win, COLOR_PAIR(COLOR_PAIR_WINDOW));

	begin_y = begin_x = 0;

	if (borders) {
		wborder(win,
			bdr[BORDER_LS], bdr[BORDER_RS], bdr[BORDER_TS], bdr[BORDER_BS],
			bdr[BORDER_TL], bdr[BORDER_TR], bdr[BORDER_BL], bdr[BORDER_BR]);
		ncols -= 2;
		begin_y = begin_x = 1;
	}

	if (text) {
		askpass->text = message_new(win, begin_y, begin_x, text);
		free(text);

		if (!askpass->text)
			goto fail;

		begin_y += widget_lines(askpass->text);
	}

	if (label) {
		askpass->label = message_new(win, begin_y, begin_x, label);
		free(label);

		if (!askpass->label)
			goto fail;

		begin_y += widget_lines(askpass->label) - 1;
		begin_x += widget_cols(askpass->label);
		ncols   -= widget_cols(askpass->label);
	}

	askpass->input = input_new(win, begin_y, begin_x, ncols);
	if (!askpass->input)
		goto fail;

	askpass->input->force_chr = L'*';

	panel = new_panel(win);
	if (panel) {
		set_panel_userptr(panel, askpass);
		return panel;
	}
fail:
	if (askpass) {
		message_free(askpass->text);
		message_free(askpass->label);
		input_free(askpass->input);
		free(askpass);
	}
	if (panel)
		del_panel(panel);
	if (win)
		delwin(win);

	return NULL;
}

static enum p_retcode p_askpass_delete(PANEL *panel)
{
	struct askpass *askpass = (struct askpass *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	message_free(askpass->text);
	message_free(askpass->label);
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

	if (askpass->input->finished)
		return P_RET_ERR;

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
