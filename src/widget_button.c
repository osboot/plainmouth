// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "plugin.h"
#include "widget.h"

bool button_focus(struct button *btn, bool in_focus)
{
	if (in_focus) {
		mvwaddch(btn->win, 0, 0, '[');
		mvwaddch(btn->win, 0, btn->width - 1, ']');
	} else {
		mvwaddch(btn->win, 0, 0, ' ');
		mvwaddch(btn->win, 0, btn->width - 1, ' ');
	}
	wnoutrefresh(btn->win);

	return true;
}

int button_len(const char *label)
{
	size_t mbslen = mbstowcs(NULL, label, 0);

	if (mbslen == (size_t) -1) {
		warn("mbstowcs");
		return 0;
	}

	// "[" + label + "]"
	return (int) mbslen + 2;
}

struct button *button_new(struct buttons *buttons, WINDOW *parent,
		int begin_y, int begin_x, const wchar_t *label)
{
	int width = (int) wcslen(label) + 2;
	struct button *btn = calloc(1, sizeof(*btn));

	if (parent)
		btn->win = derwin(parent, 1, width, begin_y, begin_x);
	else
		btn->win = newwin(1, width, begin_y, begin_x);

	btn->width = width;
	btn->on_change = &button_focus;

	wbkgd(btn->win, COLOR_PAIR(COLOR_PAIR_BUTTON));
	mvwprintw(btn->win, 0, 0, " %ls ", label);

	if (buttons)
		TAILQ_INSERT_TAIL(buttons, btn, entries);

	return btn;
}

void buttons_free(struct buttons *buttons)
{
	struct button *b1, *b2;

	b1 = TAILQ_FIRST(buttons);
	while (b1) {
		b2 = TAILQ_NEXT(b1, entries);
		delwin(b1->win);
		free(b1);
		b1 = b2;
	}
}
