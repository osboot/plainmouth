// SPDX-License-Identifier: GPL-2.0-or-later

#include <sys/queue.h>
#include <unistd.h>
#include <stdbool.h>
#include <err.h>

#include <curses.h>

#include "helpers.h"
#include "plugin.h"
#include "widget.h"

struct input *input_new(WINDOW *parent, int begin_y, int begin_x, int width)
{
	struct input *input = calloc(1, sizeof(*input));

	if (!input) {
		warnx("calloc failed");
		return NULL;
	}

	input->win = derwin(parent, 1, width, begin_y, begin_x);
	wbkgd(input->win, COLOR_PAIR(COLOR_PAIR_BUTTON));
	wmove(input->win, 0, 0);

	return input;
}

void input_free(struct input *input)
{
	if (!input)
		return;

	delwin(input->win);

	free(input->data);
	free(input);
}

static bool __input_unchr(struct input *input)
{
	if (input->len > 0)
		input->data[--input->len] = '\0';
	return true;
}

static bool __input_append(struct input *input, wchar_t c)
{
	int len = input->len + 2;

	if (input->cap < len) {
		wchar_t *data = realloc(input->data, (size_t) len * sizeof(wchar_t));

		if (!data) {
			warn("realloc");
			return false;
		}
		input->data = data;
		input->cap = len;
	}

	input->data[input->len++] = c;
	input->data[input->len] = L'\0';

	return true;
}

bool input_wchar(struct input *input, wchar_t c)
{
	if (input->finished)
		return true;

	switch (c) {
		case KEY_ENTER:
		case L'\n':
			input->finished = true;
			break;

		case KEY_BACKSPACE:
		case L'\b':
		case 127:
			if (!__input_unchr(input))
				return false;
			break;

		default:
			if(!__input_append(input, c))
				return false;
	}

	int i;
	int max_x = widget_cols(input);
	int width = MIN(input->len, max_x);

	wmove(input->win, 0, 0);
	wclrtoeol(input->win);

	for (i = 0; i < width; i++)
		wprintw(input->win, "%lc", (input->force_chr ?: input->data[i]));

	if (i == max_x)
		wmove(input->win, 0, max_x - 1);

	wnoutrefresh(input->win);
	return true;
}
