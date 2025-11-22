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

struct pass {
	struct mainwin mainwin;
	struct message *text;
	struct message *label;
	struct input   *input;
};

static PANEL *p_pass_create(struct request *req)
{
	PANEL *panel = NULL;
	struct pass *pass;

	pass = calloc(1, sizeof(*pass));
	if (!pass) {
		warn("calloc pass");
		return NULL;
	}

	wchar_t *text  __free(ptr) = req_get_wchars(req, "text");
	wchar_t *label __free(ptr) = req_get_wchars(req, "label");

	int nlines = 0;
	int ncols = 0;

	int lbl_nlines, lbl_ncols;
	int txt_nlines, txt_ncols;

	text_size(label, &lbl_nlines, &lbl_ncols);
	text_size(text, &txt_nlines, &txt_ncols);

	nlines = MAX(nlines, txt_nlines) + (lbl_nlines ?: 1);
	ncols  = MAX(ncols, txt_ncols);

	if (ncols < (lbl_ncols + MIN_INPUT_COLS))
		ncols = lbl_ncols + MIN_INPUT_COLS;

	if (!mainwin_new(req, &pass->mainwin, nlines, ncols)) {
		free(pass);
		return NULL;
	}

	int begin_y = 0;
	int begin_x = 0;

	if (text) {
		pass->text = message_new(widget_win(&pass->mainwin), begin_y, begin_x, text);
		if (!pass->text)
			goto fail;

		begin_y += widget_lines(pass->text);
	}

	if (label) {
		pass->label = message_new(widget_win(&pass->mainwin), begin_y, begin_x, label);
		if (!pass->label)
			goto fail;

		begin_y += widget_lines(pass->label) - 1;
		begin_x += widget_cols(pass->label);
		ncols   -= widget_cols(pass->label);
	}

	pass->input = input_new(widget_win(&pass->mainwin), begin_y, begin_x, ncols);
	if (!pass->input)
		goto fail;

	pass->input->force_chr = L'*';

	if ((panel = mainwin_panel_new(&pass->mainwin, pass)) != NULL)
		return panel;
fail:
	if (pass) {
		message_free(pass->text);
		message_free(pass->label);
		input_free(pass->input);
		mainwin_free(&pass->mainwin);
		free(pass);
	}
	return NULL;
}

static enum p_retcode p_pass_delete(PANEL *panel)
{
	struct pass *pass = (struct pass *) panel_userptr(panel);

	message_free(pass->text);
	message_free(pass->label);
	input_free(pass->input);

	mainwin_free(&pass->mainwin);
	mainwin_panel_free(panel);

	return P_RET_OK;
}

static enum p_retcode p_pass_input(PANEL *panel, wchar_t code)
{
	struct pass *pass = (struct pass *) panel_userptr(panel);

	return input_wchar(pass->input, code)
		? P_RET_OK : P_RET_ERR;
}

static enum p_retcode p_pass_get_cursor(PANEL *panel, int *y, int *x)
{
	const struct pass *pass = panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	if (pass->input->finished)
		return P_RET_ERR;

	return get_abs_cursor(win, pass->input->win, y, x)
		? P_RET_OK : P_RET_ERR;
}

static enum p_retcode p_pass_result(struct request *req, PANEL *panel)
{
	const struct pass *pass = panel_userptr(panel);

	ipc_send_string(req_fd(req), "RESPDATA %s PASSWORD=%ls", req_id(req), pass->input->data ?: L"");
	ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_0=%d", req_id(req), pass->input->finished);

	return P_RET_OK;
}

static bool p_pass_finished(PANEL *panel)
{
	const struct pass *pass = panel_userptr(panel);

	return pass->input->finished;
}

struct plugin plugin = {
	.name              = "password",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_pass_create,
	.p_delete_instance = p_pass_delete,
	.p_update_instance = NULL,
	.p_input           = p_pass_input,
	.p_finished        = p_pass_finished,
	.p_result          = p_pass_result,
	.p_get_cursor      = p_pass_get_cursor,
};
