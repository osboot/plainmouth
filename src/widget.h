// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <stdbool.h>
#include <ncurses.h>

#include "request.h"

struct borders {
	const char *name;
	chtype chr;
};

enum boders_geometric {
	BORDER_LS = 0,
	BORDER_RS,
	BORDER_TS,
	BORDER_BS,
	BORDER_TL,
	BORDER_TR,
	BORDER_BL,
	BORDER_BR,
	BORDER_SIZE,
};

int widget_round(float number);
void widget_begin_yx(int width, bool border, int *begin_y, int *begin_x);
void widget_text_lines(const char *text, int *num_lines, int *max_width);
void widget_mvwtext(WINDOW *win, int y, int x, const char *text);
bool widget_borders(struct request *req, chtype bdr[BORDER_SIZE]);

#endif /* _PLAINMOUTH_WIDGET_H_ */
