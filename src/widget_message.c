// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "helpers.h"
#include "widget.h"

struct message *message_new(WINDOW *parent, int begin_y, int begin_x, const wchar_t *text)
{
	struct message *msg = calloc(1, sizeof(*msg));

	if (!msg) {
		warnx("calloc failed");
		return NULL;
	}

	int nlines, ncols;
	text_size(text, &nlines, &ncols);

	msg->win = window_new(parent, nlines, ncols, begin_y, begin_x, "message");
	if (!msg->win) {
		free(msg);
		return NULL;
	}

	write_mvwtext(msg->win, 0, 0, text);
	wmove(msg->win, 0, 0);
	return msg;
}

void message_free(struct message *msg)
{
	if (msg) {
		window_free(msg->win, "message");
		free(msg);
	}
}
