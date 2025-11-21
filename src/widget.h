// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <sys/queue.h>
#include <stdbool.h>

#include <curses.h>
#include <panel.h>

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
void focus_free(struct focuses *focuses);
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

bool widget_borders(struct request *req, chtype bdr[BORDER_SIZE]);

enum {
	COLOR_PAIR_MAIN = 1,
	COLOR_PAIR_WINDOW,
	COLOR_PAIR_BUTTON,
};

int simple_round(float number);
void position_center(int width, int height, int *begin_y, int *begin_x);
bool get_abs_cursor(WINDOW *target, WINDOW *win, int *cursor_y, int *cursor_x);
void text_size(const wchar_t *text, int *lines, int *columns);
void write_mvwtext(WINDOW *win, int y, int x, const wchar_t *text);

struct mainwin {
	WINDOW *_main;
	WINDOW *win;
};

#define widget_win(w)		((w)->win)
#define widget_cols(w)		getmaxx(widget_win(w))
#define widget_lines(w)		getmaxy(widget_win(w))

bool mainwin_new(struct request *req, struct mainwin *w, int def_nlines, int def_ncols);
void mainwin_free(struct mainwin *w);
PANEL *mainwin_panel(struct mainwin *w);

// widget_button.c

struct button {
	TAILQ_ENTRY(button) entries;
	WINDOW *win;
	int width;
	bool clicked;

	bool (*on_change)(struct button *btn, bool in_focus);
};

TAILQ_HEAD(buttons, button);

static inline void buttons_init(struct buttons *buttons)
{
	TAILQ_INIT(buttons);
}

bool button_focus(struct button *btn, bool in_focus);
int button_len(const char *label);
struct button *button_new(struct buttons *buttons, WINDOW *parent, int begin_y, int begin_x, const wchar_t *label);
void buttons_free(struct buttons *buttons);

// widget_input.c

struct input {
	WINDOW *win;

	wchar_t force_chr;

	wchar_t *data;
	int cap;
	int len;

	bool finished;
};

struct input *input_new(WINDOW *parent, int begin_y, int begin_x, int width);
void input_free(struct input *input);
bool input_wchar(struct input *input, wchar_t c);

// widget_message.c

struct message {
	WINDOW *win;
};

struct message *message_new(WINDOW *parent, int begin_y, int begin_x, const wchar_t *text);
void message_free(struct message *msg);

#endif /* _PLAINMOUTH_WIDGET_H_ */
