// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_HELP_H
#define PLAINMOUTH_DAEMON_HELP_H

#include <stdbool.h>
#include <curses.h>
#include <panel.h>

struct widget;

/* UI-thread only. The focused widget is borrowed and never owns the panel. */
struct daemon_help {
	WINDOW *win;
	PANEL *panel;
	const struct widget *context;
	bool open;
	int offset;
	int lines;
};

bool daemon_help_input(struct daemon_help *help, const struct widget *focused,
		       wchar_t key, bool keycode);
void daemon_help_render(struct daemon_help *help, const struct widget *focused);
void daemon_help_close(struct daemon_help *help);

#endif
