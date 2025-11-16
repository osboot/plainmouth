// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <err.h>

#include <curses.h>

#include "widget.h"

void focus_init(struct focuses *focuses, bool (*on_change)(void *data, bool in_focus))
{
	TAILQ_INIT(&focuses->head);
	focuses->on_change = on_change;
}

bool focus_new(struct focuses *focuses, void *data)
{
	struct focus *new = calloc(1, sizeof(*new));

	if (!new) {
		warnx("focus_new: no memory");
		return false;
	}

	new->data = data;
	TAILQ_INSERT_TAIL(&focuses->head, new, entries);

	if (focuses->on_change) {
		struct focus *curr = focus_current(focuses);
		focuses->on_change(data, (curr->data == data));
	}

	return true;
}

void focus_free(struct focuses *focuses)
{
	struct focus *f1, *f2;

	f1 = TAILQ_FIRST(&focuses->head);
	while (f1) {
		f2 = TAILQ_NEXT(f1, entries);
		free(f1);
		f1 = f2;
	}
}

struct focus *focus_current(struct focuses *focuses)
{
	return TAILQ_FIRST(&focuses->head);
}

void focus_set(struct focuses *focuses, void *data)
{
	struct focus *curr = focus_current(focuses);

	if (!curr)
		return;

	if (curr->data != data) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, curr, entries);
		TAILQ_INSERT_HEAD(&focuses->head, curr, entries);

		if (focuses->on_change) {
			curr = focus_current(focuses);
			focuses->on_change(curr->data, true);
		}
	}
}

void focus_next(struct focuses *focuses)
{
	struct focus *curr = focus_current(focuses);

	if (curr) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, curr, entries);
		TAILQ_INSERT_TAIL(&focuses->head, curr, entries);

		if (focuses->on_change) {
			curr = focus_current(focuses);
			focuses->on_change(curr->data, true);
		}
	}
}

void focus_prev(struct focuses *focuses)
{
	struct focus *prev = TAILQ_LAST(&focuses->head, focushead);
	struct focus *curr = focus_current(focuses);

	if (prev && prev != curr) {
		if (focuses->on_change)
			focuses->on_change(curr->data, false);

		TAILQ_REMOVE(&focuses->head, prev, entries);
		TAILQ_INSERT_HEAD(&focuses->head, prev, entries);

		if (focuses->on_change)
			focuses->on_change(prev->data, true);
	}
}

int widget_round(float number)
{
	// Example: 15.4 + 0.5 = 15.9 -> 15
	//          15.6 + 0.5 = 16.1 -> 16
	return (int) (number >= 0 ? number + 0.5 : number - 0.5);
}

void widget_begin_yx(int width, bool border, int *begin_y, int *begin_x)
{
	if (begin_y && *begin_y < 0)
		*begin_y = widget_round(((float) LINES / 2) - (border ? 1 : 0));

	if (begin_x && *begin_x < 0)
		*begin_x = widget_round(((float) COLS / 2) - ((float) width / 2));
}

void widget_text_lines(const wchar_t *text, int *num_lines, int *max_width)
{
	int nlines, maxwidth;
	const wchar_t *s, *e;

	nlines = maxwidth = 0;

	if (!text)
		goto empty;

	s = e = text;

	while (1) {
		if (*e == '\n' || *e == '\0') {
			if (maxwidth < (e - s)) {
				maxwidth = (int)(e - s);
				s = e + 1;
			}
			nlines += 1;
		}
		if (*e == '\0')
			break;
		e++;
	}

	if (s == text && !nlines) {
		maxwidth = (int)(e - s);
		nlines = 1;
	}
empty:
	if (num_lines)
		*num_lines = nlines;
	if (max_width)
		*max_width = maxwidth;
}

void widget_mvwtext(WINDOW *win, int y, int x, const wchar_t *text)
{
	const wchar_t *s, *e;

	if (!text)
		return;

	s = e = text;

	while (1) {
		if (*e == '\n' || *e == '\0') {
			mvwprintw(win, y, x, "%.*ls", (int)(e - s), s);
			s = e + 1;
			y += 1;
		}
		if (*e == '\0')
			break;
		e++;
	}
}

bool widget_borders(struct request *req, chtype bdr[BORDER_SIZE])
{
	struct borders borders[BORDER_SIZE] = {
		[BORDER_LS] = { "border_ls", ACS_VLINE    },
		[BORDER_RS] = { "border_rs", ACS_VLINE    },
		[BORDER_TS] = { "border_ts", ACS_HLINE    },
		[BORDER_BS] = { "border_bs", ACS_HLINE    },
		[BORDER_TL] = { "border_tl", ACS_ULCORNER },
		[BORDER_TR] = { "border_tr", ACS_URCORNER },
		[BORDER_BL] = { "border_bl", ACS_LLCORNER },
		[BORDER_BR] = { "border_br", ACS_LRCORNER },
	};
	bool res = false;

	if (!req_get_bool(req, "border", true))
		return res;

	for (int i = 0; i < BORDER_SIZE; i++) {
		if (req_get_bool(req, borders[i].name, true)) {
			res = true;
			bdr[i] = borders[i].chr;
		} else {
			bdr[i] = ' ';
		}
	}

	return res;
}
