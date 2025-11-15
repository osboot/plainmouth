// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <sys/queue.h>
#include <stdbool.h>
#include <ncurses.h>

#include "request.h"

struct focus {
	TAILQ_ENTRY(focus) entries;
	void *data;
};

TAILQ_HEAD(focushead, focus);

struct focuses {
	struct focushead head;
	bool (*on_change)(void *data, bool in_focus);
};

void focus_init(struct focuses *focuses, bool (*on_change)(void *data, bool in_focus));
bool focus_new(struct focuses *focuses, void *data);
struct focus *focus_current(struct focuses *focuses);
void focus_set(struct focuses *focuses, void *data);
void focus_next(struct focuses *focuses);
void focus_prev(struct focuses *focuses);

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
void widget_text_lines(const wchar_t *text, int *num_lines, int *max_width);
void widget_mvwtext(WINDOW *win, int y, int x, const wchar_t *text);
bool widget_borders(struct request *req, chtype bdr[BORDER_SIZE]);

#endif /* _PLAINMOUTH_WIDGET_H_ */
