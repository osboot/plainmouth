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

struct msgbox {
	struct focuses focus;
	struct buttons buttons;
	bool finished;
};

static bool on_button_focus(void *data, bool in_focus)
{
	struct button *btn = data;
	return btn->on_change(btn, in_focus);
}

static PANEL *p_msgbox_create(struct request *req)
{
	PANEL *panel;
	chtype bdr[BORDER_SIZE];
	struct msgbox *msgbox;
	struct ipc_pair *p = req_data(req);

	msgbox = calloc(1, sizeof(*msgbox));
	if (!msgbox) {
		warn("calloc msgbox");
		return NULL;
	}

	focus_init(&msgbox->focus, &on_button_focus);
	buttons_init(&msgbox->buttons);

	wchar_t *text = req_get_wchars(req, "text");
	int begin_x = req_get_int(req, "x", -1);
	int begin_y = req_get_int(req, "y", -1);
	int nlines = req_get_int(req, "height", -1);
	int ncols = req_get_int(req, "width", -1);
	bool borders = widget_borders(req, bdr);

	int buttons_len = 0;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "button"))
			buttons_len += button_len(p->kv[i].val) + 1;
	}

	int txt_nlines, txt_ncols;

	text_size(text, &txt_nlines, &txt_ncols);

	nlines = MAX(nlines, txt_nlines);
	ncols  = MAX(ncols, txt_ncols);
	ncols  = MAX(ncols, buttons_len);

	if (buttons_len > 0) {
		ncols  -= 1;
		nlines += 1;
	}

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

	begin_y = begin_x = 0;

	if (borders)
		begin_y = begin_x = 1;

	if (text) {
		write_mvwtext(win, begin_y, begin_x, text);
		free(text);
	}

	begin_y += txt_nlines;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (!streq(p->kv[i].key, "button"))
			continue;

		wchar_t *wcs = req_get_kv_wchars(p->kv + i);

		struct button *btn = button_new(&msgbox->buttons, win, begin_y, begin_x, wcs);
		free(wcs);

		focus_new(&msgbox->focus, btn);

		begin_x += btn->width + 1;
	}

	panel = new_panel(win);
	set_panel_userptr(panel, msgbox);

	return panel;
}

static enum p_retcode p_msgbox_delete(PANEL *panel)
{
	struct msgbox *msgbox = (struct msgbox *) panel_userptr(panel);
	WINDOW *win = panel_window(panel);

	buttons_free(&msgbox->buttons);
	focus_free(&msgbox->focus);

	del_panel(panel);
	delwin(win);

	free(msgbox);

	return P_RET_OK;
}

static enum p_retcode p_msgbox_input(PANEL *panel, wchar_t code)
{
	struct msgbox *msgbox = (struct msgbox *) panel_userptr(panel);

	if (msgbox->finished)
		return P_RET_OK;

	switch (code) {
		case KEY_ENTER:
		case L'\n':
			{
				struct focus *curr = focus_current(&msgbox->focus);
				if (curr) {
					struct button *btn = curr->data;
					btn->clicked = true;
					msgbox->finished = true;
				}
			}
			break;

		case KEY_LEFT:
		case L'<':
			focus_prev(&msgbox->focus);
			break;

		case KEY_RIGHT:
		case L'>':
			focus_next(&msgbox->focus);
			break;

		default:
			warnx("msgbox input [%04x]", code);
			fflush(stderr);
	}

	return P_RET_OK;
}

static enum p_retcode p_msgbox_result(struct request *req, PANEL *panel)
{
	const struct msgbox *msgbox = panel_userptr(panel);

	int i = 0;
	struct button *btn;

	TAILQ_FOREACH(btn, &msgbox->buttons, entries) {
		ipc_send_string(req_fd(req), "RESPDATA %s BUTTON_%d=%d", req_id(req), i, btn->clicked);
		i++;
	}

	return P_RET_OK;
}

static bool p_msgbox_finished(PANEL *panel)
{
	const struct msgbox *msgbox = panel_userptr(panel);
	return msgbox->finished;
}

struct plugin plugin = {
	.name              = "msgbox",
	.p_plugin_init     = NULL,
	.p_plugin_free     = NULL,
	.p_create_instance = p_msgbox_create,
	.p_delete_instance = p_msgbox_delete,
	.p_update_instance = NULL,
	.p_input           = p_msgbox_input,
	.p_finished        = p_msgbox_finished,
	.p_result          = p_msgbox_result,
	.p_get_cursor      = NULL,
};
