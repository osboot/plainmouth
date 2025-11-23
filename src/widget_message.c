// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "helpers.h"
#include "widget.h"

static void message_text(WINDOW *win, const wchar_t *text, int scroll_pos)
{
	int y, nlines, ncols, curline;
	const wchar_t *s, *e;

	if (!text)
		return;

	getmaxyx(win, nlines, ncols);
	werase(win);

	s = text;
	e = s + wcslen(s);
	y = curline = 0;

	while (s < e && y < nlines) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;
		int len = MIN((int) (c - s), ncols);

		if (curline >= scroll_pos)
			mvwaddnwstr(win, y++, 0, s, len);

		curline++;
		s = (c == e) ? e : c + 1;
	}

	wnoutrefresh(win);
}

static void draw_vscroll(WINDOW *scrollwin, int scroll_pos, int content_height)
{
	int view_height = getmaxy(scrollwin);
	int thumb_size = MAX(1, (view_height * view_height) / content_height);
	int thumb_pos = (scroll_pos * (view_height - thumb_size)) / (content_height - view_height);

	werase(scrollwin);

	for (int i = 0; i < view_height; i++)
		mvwaddch(scrollwin, i, 0, ACS_CKBOARD);

	wattron(scrollwin, A_REVERSE);
	for (int i = 0; i < thumb_size; i++)
		mvwaddch(scrollwin, thumb_pos + i, 0, ' ');
	wattroff(scrollwin, A_REVERSE);

	wnoutrefresh(scrollwin);
}

struct message *message_new(WINDOW *parent, int begin_y, int begin_x, wchar_t *text)
{
	int par_nlines, par_ncols;
	struct message *msg = calloc(1, sizeof(*msg));

	if (!msg) {
		warnx("calloc failed");
		return NULL;
	}
	msg->text = text;

	text_size(msg->text, &msg->text_nlines, &msg->text_ncols);
	getmaxyx(parent, par_nlines, par_ncols);

	par_nlines -= 1;
	par_ncols  -= 1;

	int nlines = (msg->text_nlines > par_nlines) ? par_nlines : msg->text_nlines;
	int ncols  = (msg->text_ncols  < par_ncols)  ? par_ncols  : msg->text_ncols;

	if (nlines < msg->text_nlines) {
		ncols -= 1;
		msg->vscroll = window_new(parent, nlines, 1, begin_y, begin_x + ncols, "vertical scroll");
		if (!msg->vscroll)
			goto fail;

		draw_vscroll(msg->vscroll, msg->vscroll_pos, msg->text_nlines);
	}

	msg->win = window_new(parent, nlines, ncols, begin_y, begin_x, "message");
	if (!msg->win)
		goto fail;

	message_text(msg->win, msg->text, msg->vscroll_pos);

	return msg;
fail:
	message_free(msg);
	return NULL;
}

void message_free(struct message *msg)
{
	if (msg) {
		window_free(msg->win, "message");
		window_free(msg->vscroll, "vertical scroll");
		free(msg->text);
		free(msg);
	}
}

void message_key(struct message *msg, wchar_t code)
{
	if (!msg->vscroll)
		return;

	int view_height = getmaxy(msg->vscroll);

	switch (code) {
		case KEY_UP:
			if (msg->vscroll_pos > 0)
				msg->vscroll_pos--;
			break;

		case KEY_DOWN:
			if (msg->vscroll_pos < msg->text_nlines - view_height)
				msg->vscroll_pos++;
			break;

		case KEY_PPAGE:
			msg->vscroll_pos = MAX(0, msg->vscroll_pos - view_height);
			break;

		case KEY_NPAGE:
			msg->vscroll_pos = MIN(msg->text_nlines - view_height, msg->vscroll_pos + view_height);
			break;
		default:
			return;
	}

	message_text(msg->win, msg->text, msg->vscroll_pos);
	draw_vscroll(msg->vscroll, msg->vscroll_pos, msg->text_nlines);
}
