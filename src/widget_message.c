// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "helpers.h"
#include "widget.h"

static wchar_t *wcsndup(const wchar_t *s, size_t n)
{
	wchar_t *t = calloc((n + 1), sizeof(wchar_t));
	if (t) {
		wmemcpy(t, s, n);
		t[n] = L'\0';
	}
	return t;
}

void viewport_create(struct text_viewport *vp, const wchar_t *text)
{
	const wchar_t *s = text;
	const wchar_t *e = text + wcslen(text);

	int capacity = 128;

	vp->ncols = 0;
	vp->nlines = 0;
	vp->lines = calloc((size_t) capacity, sizeof(wchar_t *));

	if (!vp->lines) {
		warn("calloc failed");
		return;
	}

	while (s < e) {
		const wchar_t *c = wcschr(s, L'\n') ?: e;
		size_t len = (size_t) (c - s);
		wchar_t *line = wcsndup(s, len);

		if (!line) {
			warnx("unable to duplicate string");
			viewport_free(vp);
			return;
		}

		if (vp->nlines == capacity) {
			wchar_t **newlines;

			capacity *= 2;
			newlines = realloc(vp->lines, sizeof(wchar_t *) * (size_t) capacity);

			if (!newlines) {
				warn("realloc failed");
				viewport_free(vp);
				return;
			}
			vp->lines = newlines;
		}

		vp->lines[vp->nlines++] = line;
		vp->ncols = MAX(vp->ncols, (int) len);

		s = (c == e) ? e : c + 1;
	}
}

void viewport_free(struct text_viewport *vp)
{
	for (int i = 0; i < vp->nlines; i++)
		free(vp->lines[i]);
	free(vp->lines);
}

void viewport_draw(WINDOW *win, struct text_viewport *vp, int scroll_pos)
{
	int nlines, ncols, y = 0;
	getmaxyx(win, nlines, ncols);
	werase(win);

	for (int i = scroll_pos; i < vp->nlines && y < nlines; i++)
		mvwaddnwstr(win, y++, 0, vp->lines[i], ncols);

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

struct message *message_new(WINDOW *parent, int begin_y, int begin_x, int nlines, int ncols, wchar_t *text)
{
	int par_nlines, par_ncols;
	struct message *msg = calloc(1, sizeof(*msg));

	if (!msg) {
		warnx("calloc failed");
		return NULL;
	}

	viewport_create(&msg->text, text);

	getmaxyx(parent, par_nlines, par_ncols);

	par_nlines -= 1;
	par_ncols  -= 1;

	if (nlines < 0) nlines = (msg->text.nlines > par_nlines) ? par_nlines : msg->text.nlines;
	if (ncols  < 0) ncols  = (msg->text.ncols  < par_ncols)  ? par_ncols  : msg->text.ncols;

	if (nlines > par_nlines) nlines = par_nlines;
	if (ncols  > par_ncols ) ncols  = par_ncols;

	if (nlines < msg->text.nlines) {
		ncols -= 1;
		msg->vscroll = window_new(parent, nlines, 1, begin_y, begin_x + ncols, "vertical scroll");
		if (!msg->vscroll)
			goto fail;

		draw_vscroll(msg->vscroll, msg->vscroll_pos, msg->text.nlines);
	}

	msg->win = window_new(parent, nlines, ncols, begin_y, begin_x, "message");
	if (!msg->win)
		goto fail;

	viewport_draw(msg->win, &msg->text, msg->vscroll_pos);

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
		viewport_free(&msg->text);
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
			if (msg->vscroll_pos < msg->text.nlines - view_height)
				msg->vscroll_pos++;
			break;

		case KEY_PPAGE:
			msg->vscroll_pos = MAX(0, msg->vscroll_pos - view_height);
			break;

		case KEY_NPAGE:
			msg->vscroll_pos = MIN(msg->text.nlines - view_height, msg->vscroll_pos + view_height);
			break;
		default:
			return;
	}

	viewport_draw(msg->win, &msg->text, msg->vscroll_pos);
	draw_vscroll(msg->vscroll, msg->vscroll_pos, msg->text.nlines);
}
