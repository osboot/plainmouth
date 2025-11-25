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
	struct message *text;
	struct mainwin mainwin;
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
	struct msgbox *msgbox;
	struct ipc_pair *p = req_data(req);

	msgbox = calloc(1, sizeof(*msgbox));
	if (!msgbox) {
		warn("calloc msgbox");
		return NULL;
	}

	focus_init(&msgbox->focus, &on_button_focus);
	buttons_init(&msgbox->buttons);

	wchar_t *text __free(ptr) = req_get_wchars(req, "text");

	int nlines = 0;
	int ncols = 0;
	int buttons_len = 0;

	text_size(text, &nlines, &ncols);

	for (size_t i = 0; i < p->num_kv; i++) {
		if (streq(p->kv[i].key, "button"))
			buttons_len += button_len(p->kv[i].val) + 1;
	}

	if (buttons_len > 0) {
		ncols  = MAX(ncols, buttons_len - 1);
		nlines += 1;
	}

	if (!mainwin_new(req, &msgbox->mainwin, nlines, ncols)) {
		free(msgbox);
		return NULL;
	}

	int begin_x = 0;
	int begin_y = 0;

	if (text) {
		msgbox->text = message_new(msgbox->mainwin.win, begin_y, begin_x, -1, -1, text);
		if (!msgbox->text)
			goto fail;

		begin_y += msgbox->text->nlines;
	}

	for (size_t i = 0; i < p->num_kv; i++) {
		if (!streq(p->kv[i].key, "button"))
			continue;

		wchar_t *wcs = req_get_kv_wchars(p->kv + i);

		struct button *btn = button_new(&msgbox->buttons, msgbox->mainwin.win, begin_y, begin_x, wcs);
		free(wcs);

		focus_new(&msgbox->focus, btn);

		begin_x += btn->ncols + 1;
	}

	if ((panel = mainwin_panel_new(&msgbox->mainwin, msgbox)) != NULL)
		return panel;
fail:
	if (msgbox) {
		message_free(msgbox->text);
		free(msgbox);
	}
	return NULL;
}

static enum p_retcode p_msgbox_delete(PANEL *panel)
{
	struct msgbox *msgbox = (struct msgbox *) panel_userptr(panel);

	message_free(msgbox->text);
	buttons_free(&msgbox->buttons);
	focus_free(&msgbox->focus);

	mainwin_free(&msgbox->mainwin);
	mainwin_panel_free(panel);

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

		case KEY_UP:
		case KEY_DOWN:
		case KEY_PPAGE:
		case KEY_NPAGE:
			if (msgbox->text)
				message_key(msgbox->text, code);
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
