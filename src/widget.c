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
