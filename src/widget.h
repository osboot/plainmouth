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
	COLOR_PAIR_EXTRA1,
	COLOR_PAIR_EXTRA2,
	COLOR_PAIR_EXTRA3,
	COLOR_PAIR_EXTRA4,
};

int simple_round(float number);
void position_center(int width, int height, int *begin_y, int *begin_x);
bool get_abs_cursor(WINDOW *target, WINDOW *win, int *cursor_y, int *cursor_x);
void text_size(const wchar_t *text, int *lines, int *columns);

WINDOW *window_new(WINDOW *parent, int nlines, int ncols, int begin_y, int begin_x, const char *what);
void window_free(WINDOW *win, const char *what);

struct mainwin {
	int nlines;
	int ncols;

	WINDOW *_main;
	WINDOW *win;
};

bool mainwin_new(struct request *req, struct mainwin *w, int def_nlines, int def_ncols);
void mainwin_free(struct mainwin *w);

PANEL *mainwin_panel_new(struct mainwin *w, const void *data);
void mainwin_panel_free(PANEL *panel);

// widget_message.c

struct text_viewport {
	wchar_t **lines;
	int nlines;
	int ncols;
};

void viewport_create(struct text_viewport *vp, const wchar_t *text);
void viewport_free(struct text_viewport *vp);
void viewport_draw(WINDOW *win, struct text_viewport *vp, int scroll_pos);

struct message {
	int nlines;
	int ncols;

	WINDOW *win;
	WINDOW *vscroll;
	int vscroll_pos;

	struct text_viewport text;
};

struct message *message_new(WINDOW *parent, int begin_y, int begin_x, int nlines, int ncols, wchar_t *text);
void message_free(struct message *msg);
void message_key(struct message *msg, wchar_t key);

// widget_button.c

struct button {
	TAILQ_ENTRY(button) entries;

	int nlines;
	int ncols;

	WINDOW *win;
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
	int nlines;
	int ncols;

	WINDOW *win;
	struct message *label;

	wchar_t force_chr;

	wchar_t *data;
	int cap;
	int len;

	bool finished;
};

struct input *input_new(WINDOW *parent, int begin_y, int begin_x, int width, wchar_t *label);
void input_free(struct input *input);
bool input_wchar(struct input *input, wchar_t c);

enum widget_type {
	WIDGET_WINDOW,
	WIDGET_LABEL,
	WIDGET_BUTTON,
	WIDGET_INPUT,
	WIDGET_VBOX,
	WIDGET_HBOX
};

struct widget;
TAILQ_HEAD(widgethead, widget);

struct widget {
	TAILQ_ENTRY(widget) siblings;

	enum widget_type type;

	// geometry
	int x, y, w, h;
	int min_w, min_h;

	// ncurses handles
	WINDOW *win;

	// tree
	struct widget *parent;
	struct widgethead children;

	// focus
	//int can_focus;
	//int focused;

	// methods
	void (*measure)(struct widget *);
	void (*layout)(struct widget *);
	void (*render)(struct widget *);
	int (*on_key)(struct widget *, int);

	// widget-specific data
	void *data;
};

struct widget *widget_create(enum widget_type type);
void widget_add(struct widget *parent, struct widget *child);
void widget_free(struct widget *w);
const char *widget_type(struct widget *w);
bool widget_window(struct widget *w);
void widget_measure(struct widget *w);
void widget_layout(struct widget *w, int x, int y, int width, int height);
void widget_render(struct widget *w);

struct widget *make_window(void);
struct widget *make_vbox(void);
struct widget *make_button(const char *label);

#endif /* _PLAINMOUTH_WIDGET_H_ */
