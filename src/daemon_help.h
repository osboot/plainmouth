// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_DAEMON_HELP_H
#define PLAINMOUTH_DAEMON_HELP_H

#include <stdbool.h>
#include <time.h>
#include <curses.h>
#include <panel.h>

#include "daemon_animation.h"

struct widget;

/* UI-thread only. The focused widget is borrowed and never owns the panel. */
struct daemon_help {
	WINDOW *win;
	PANEL *panel;
	WINDOW *content;
	const struct widget *context;
	bool open;
	int offset;
	int lines;
	enum daemon_animation animation;
	int duration_ms;
	bool animating;
	struct timespec started;
	int frame_ms;
	int rows;
	int columns;
};

bool daemon_help_input(struct daemon_help *help, const struct widget *focused,
		       wchar_t key, bool keycode);
void daemon_help_render(struct daemon_help *help, const struct widget *focused);
void daemon_help_close(struct daemon_help *help);
/* Milliseconds to the next animation frame, or -1 while idle. */
int daemon_help_timeout(struct daemon_help *help);

#endif
