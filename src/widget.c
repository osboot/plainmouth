// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdbool.h>

#include <curses.h>

#include "widget.h"

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

void widget_text_lines(const char *text, int *num_lines, int *max_width)
{
	int nlines, maxwidth;
	const char *s, *e;

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

void widget_mvwtext(WINDOW *win, int y, int x, const char *text)
{
	const char *s, *e;

	if (!text)
		return;

	s = e = text;

	while (1) {
		if (*e == '\n' || *e == '\0') {
			mvwaddnstr(win, y, x, s, (int)(e - s));
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
