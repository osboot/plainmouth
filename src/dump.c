// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <err.h>

#include <curses.h>

#include "macros.h"
#include "widget.h"

struct view {
	cchar_t *data;
	int h, w;
};

struct coord {
	int y, x;
};

struct rect {
	int y0, x0;
	int y1, x1;
};

static inline void rect_intersect(struct rect a, struct rect b, struct rect *o)
{
	o->y0 = MAX(a.y0, b.y0);
	o->x0 = MAX(a.x0, b.x0);
	o->y1 = MIN(a.y1, b.y1);
	o->x1 = MIN(a.x1, b.x1);
}

static inline bool rect_empty(struct rect r)
{
	return r.y0 >= r.y1 || r.x0 >= r.x1;
}

static void clear_view(struct view *view)
{
	cchar_t empty;
	wchar_t wc[2] = { L' ', L'\0' };

	setcchar(&empty, wc, 0, 0, NULL);

	for (int y = 0; y < view->h; y++) {
		for (int x = 0; x < view->w; x++) {
			view->data[(y * view->w) + x] = empty;
		}
	}
}

static void view_widget(struct widget *w, struct view *view, struct coord origin, struct rect clip)
{
	struct coord my = {
		.y = origin.y + w->ly,
		.x = origin.x + w->lx,
	};

	struct rect self = {
		.y0 = my.y,
		.x0 = my.x,
		.y1 = my.y + w->h,
		.x1 = my.x + w->w,
	};

	struct rect draw;

	rect_intersect(self, clip, &draw);

	if (rect_empty(draw))
		return;

	if (w->win) {
		for (int ty = draw.y0; ty < draw.y1; ty++) {
			int wy = ty - my.y;

			for (int tx = draw.x0; tx < draw.x1; tx++) {
				int wx = tx - my.x;

				cchar_t cc;
				if (mvwin_wch(w->win, wy, wx, &cc) == OK)
					view->data[(ty * view->w) + tx] = cc;
			}
		}
	}

	struct widget *c;
	TAILQ_FOREACH(c, &w->children, siblings) {
		view_widget(c, view, my, draw);
	}
}

/*
 * Software compositor for widget tree.
 *
 * view[y][x] corresponds to root->win[y][x]
 * No screen coordinates involved.
 */
void widget_dump(FILE *fd, struct widget *w)
{
	struct view view = {
		.h = w->h,
		.w = w->w,
		.data = calloc((size_t) (w->h * w->w), sizeof(cchar_t)),
	};

	if (!view.data) {
		warn("widget_dump: calloc failed");
		return;
	}

	struct coord origin = {
		.y = -w->ly,
		.x = -w->lx,
	};

	struct rect clip = {
		.y0 = 0,
		.x0 = 0,
		.y1 = w->h,
		.x1 = w->w,
	};

	clear_view(&view);
	view_widget(w, &view, origin, clip);

	int i;

	fputc('+', fd);
	for (i = 0; i < w->w; i++)
		fputc('-', fd);
	fputc('+', fd);
	fputc('\n', fd);

	for (int y = 0; y < w->h; y++) {
		fputc('|', fd);

		for (int x = 0; x < w->w; x++) {
			attr_t attrs;
			short pair;
			wchar_t wc[CCHARW_MAX + 2];

			if (getcchar(&view.data[(y * w->w) + x], wc, &attrs, &pair, NULL) == OK && wc[0] != L'\0') {
				if (attrs & A_ALTCHARSET) {
					switch (wc[0]) {
						case 'q': wc[0] = L'─'; break; /* ACS_HLINE */
						case 'x': wc[0] = L'│'; break; /* ACS_VLINE */
						case 'l': wc[0] = L'┌'; break;
						case 'k': wc[0] = L'┐'; break;
						case 'm': wc[0] = L'└'; break;
						case 'j': wc[0] = L'┘'; break;
						default:  wc[0] = L'#'; break;
					}
					wc[1] = L'\0';
				}
			} else {
				wc[0] = L' ';
				wc[1] = L'\0';
			}
			fprintf(fd, "%ls", wc);
		}

		fputc('|', fd);
		fputc('\n', fd);
	}
	free(view.data);

	fputc('+', fd);
	for (i = 0; i < w->w; i++)
		fputc('-', fd);
	fputc('+', fd);
	fputc('\n', fd);
}
