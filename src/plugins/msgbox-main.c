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

struct button {
	TAILQ_ENTRY(button) entries;
	WINDOW *win;
	int width;
	bool clicked;
};

TAILQ_HEAD(buttons, button);

struct msgbox {
	struct focuses focus;
	struct buttons buttons;
	bool finished;
};

static bool on_button_focus(void *data, bool in_focus)
{
	struct button *btn = data;

	if (in_focus) {
		mvwaddch(btn->win, 0, 0, '[');
		mvwaddch(btn->win, 0, btn->width - 1, ']');
	} else {
		mvwaddch(btn->win, 0, 0, ' ');
		mvwaddch(btn->win, 0, btn->width - 1, ' ');
	}
	wnoutrefresh(btn->win);

	return true;
}

static int button_len(const char *label)
{
	size_t mbslen = mbstowcs(NULL, label, 0);

	if (mbslen == (size_t) -1) {
		warn("mbstowcs");
		return 0;
	}

	// "[" + label + "]"
	return (int) mbslen + 2;
}

static struct button *create_button(WINDOW *parent, int begin_y, int begin_x, const wchar_t *label)
{
	int width = (int) wcslen(label) + 2;
	struct button *btn = calloc(1, sizeof(*btn));

	btn->win = derwin(parent, 1, width, begin_y, begin_x);
	btn->width = width;

	wbkgd(btn->win, COLOR_PAIR(COLOR_PAIR_BUTTON));
	mvwprintw(btn->win, 0, 0, " %ls ", label);

	return btn;
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
	TAILQ_INIT(&msgbox->buttons);

	wchar_t *label = req_get_wchars(req, "label");
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

	int lbl_nlines, lbl_maxwidth;

	widget_text_lines(label, &lbl_nlines, &lbl_maxwidth);

	nlines = MAX(nlines, lbl_nlines);
	ncols  = MAX(ncols, lbl_maxwidth);
	ncols  = MAX(ncols, buttons_len);

	if (buttons_len > 0) {
		ncols  -= 1;
		nlines += 1;
	}


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

	begin_y += lbl_nlines;

	for (size_t i = 0; i < p->num_kv; i++) {
		if (!streq(p->kv[i].key, "button"))
			continue;

		wchar_t *wcs = req_get_kv_wchars(p->kv + i);

		struct button *btn = create_button(win, begin_y, begin_x, wcs);
		free(wcs);

		TAILQ_INSERT_TAIL(&msgbox->buttons, btn, entries);
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

	struct button *b1, *b2;

	b1 = TAILQ_FIRST(&msgbox->buttons);
	while (b1) {
		b2 = TAILQ_NEXT(b1, entries);
		delwin(b1->win);
		b1 = b2;
	}

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
