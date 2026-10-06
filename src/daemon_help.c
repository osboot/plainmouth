// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <err.h>

#include "daemon_help.h"
#include "macros.h"
#include "widget.h"

static void release_window(struct daemon_help *help)
{
	if (help->panel) {
		if (del_panel(help->panel) == ERR)
			warnx("unable to destroy help panel");
		help->panel = NULL;
	}
	if (help->win) {
		if (delwin(help->win) == ERR)
			warnx("unable to destroy help window");
		help->win = NULL;
	}
}

void daemon_help_close(struct daemon_help *help)
{
	release_window(help);
	*help = (struct daemon_help) { 0 };
}

bool daemon_help_input(struct daemon_help *help, const struct widget *focused,
		       wchar_t key, bool keycode)
{
	if (!focused || focused->type == WIDGET_TERMINAL) {
		daemon_help_close(help);
		return false;
	}

	if (keycode && key == KEY_F(1)) {
		if (help->open)
			daemon_help_close(help);
		else
			help->open = true;
		return true;
	}

	if (!help->open)
		return false;

	if (!keycode && key == 27) {
		daemon_help_close(help);
		return true;
	}

	if (keycode && (key == KEY_UP || key == KEY_DOWN)) {
		int height = help->win ? getmaxy(help->win) - 2 : 0;
		int maximum = MAX(0, help->lines - height);
		int delta = key == KEY_UP ? -1 : 1;

		help->offset = CLAMP(help->offset + delta, 0, maximum);

		return true;
	}

	return false;
}

static bool help_create_window(struct daemon_help *help)
{
	int width = help->open ? MIN(44, COLS) : 7;
	int height = help->open ? LINES : 1;
	int top = help->open ? 0 : LINES - 1;

	if (COLS < width || (help->open && (width < 4 || height < 3))) {
		release_window(help);
		return false;
	}

	if (help->win && (getmaxx(help->win) != width || getmaxy(help->win) != height ||
			  getbegx(help->win) != COLS - width || getbegy(help->win) != top))
		release_window(help);

	if (!help->win) {
		help->win = newwin(height, width, top, COLS - width);

		if (!help->win) {
			warnx("unable to create help window");
			return false;
		}

		help->panel = new_panel(help->win);

		if (!help->panel) {
			warnx("unable to create help panel");
			release_window(help);
			return false;
		}

		leaveok(help->win, TRUE);
	}

	return true;
}

static size_t collect_bindings(const struct widget *focused,
			       struct widget_keybinding *bindings, size_t capacity)
{
	static const struct widget_keybinding common[] = {
		{ L'\t',    false, "Tab",       "Next field"       },
		{ KEY_BTAB, true,  "Shift+Tab", "Previous field"   },
		{ KEY_F(1), true,  "F1",        "Close help"       },
		{ 27,       false, "Esc",       "Close help"       },
		{ KEY_UP,   true,  "Up",        "Scroll help up"   },
		{ KEY_DOWN, true,  "Down",      "Scroll help down" },
	};
	size_t count = 0;

	for (size_t i = 0; i < sizeof(common) / sizeof(*common) && count < capacity; i++)
		bindings[count++] = common[i];

	struct widget_keybinding local[32];

	size_t n = widget_keybindings(focused, local, sizeof(local) / sizeof(*local));

	for (size_t i = 0; i < n && count < capacity; i++) {
		bool shadowed = false;

		for (size_t j = 0; j < count; j++)
			if (bindings[j].key == local[i].key && bindings[j].keycode == local[i].keycode)
				shadowed = true;
		if (!shadowed)
			bindings[count++] = local[i];
	}

	return count;
}

/* Count and draw identical wrapped rows, including offscreen descriptions. */
static int render_bindings(struct daemon_help *help, const struct widget_keybinding *bindings,
			   size_t count, bool draw)
{
	int width = getmaxx(help->win) - 2;
	int height = getmaxy(help->win) - 2;
	int key_width = 0;

	for (size_t i = 0; i < count; i++)
		key_width = MAX(key_width, (int) MIN(strlen(bindings[i].name), (size_t) width));

	/* Very narrow windows use wrapped labels instead of an empty text column. */
	if (key_width + 2 >= width)
		key_width = 0;

	int description_column = key_width ? key_width + 2 : 0;
	int row = 0;

	for (size_t i = 0; i < count; i++) {
		const char *parts[] = { bindings[i].name, " : ", bindings[i].description };
		int column = 0;

		for (size_t part = 0; part < sizeof(parts) / sizeof(*parts); part++) {
			size_t length = strlen(parts[part]);
			size_t padded = part == 0 ? MAX(length, (size_t) key_width) : length;

			for (size_t character = 0; character < padded; character++) {
				if (column == width) {
					row++;
					column = part == 2 ? description_column : 0;
				}

				int y = row - help->offset;

				if (draw && y >= 0 && y < height) {
					chtype cell = character < length ? (unsigned char) parts[part][character] : ' ';

					if (part == 0 && character < length)
						cell |= A_BOLD;

					mvwaddch(help->win, y + 1, column + 1, cell);
				}
				column++;
			}
		}
		row++;
	}
	return row;
}

void daemon_help_render(struct daemon_help *help, const struct widget *focused)
{
	if (!focused || focused->type == WIDGET_TERMINAL) {
		daemon_help_close(help);
		return;
	}

	if (help->context != focused) {
		help->context = focused;
		help->offset = 0;
	}

	if (!help_create_window(help))
		return;

	if (!help->open) {
		struct widget style = { .win = help->win, .style_owner = focused };

		widget_style_apply_widget(&style, COLOR_PAIR_WINDOW);

		werase(help->win);
		waddnstr(help->win, "F1 Help", 7);

		if (top_panel(help->panel) == ERR)
			warnx("unable to raise help hint");

		return;
	}

	struct widget_keybinding bindings[32];
	size_t count = collect_bindings(focused, bindings, sizeof(bindings) / sizeof(*bindings));

	help->lines = render_bindings(help, bindings, count, false);
	help->offset = MIN(help->offset, MAX(0, help->lines - (getmaxy(help->win) - 2)));

	struct widget style = { .win = help->win, .style_owner = focused };

	widget_style_apply_widget(&style, COLOR_PAIR_WINDOW);
	werase(help->win);
	box(help->win, 0, 0);

	char title[64];

	snprintf(title, sizeof(title), " Keys: %s ", widget_type((struct widget *) focused));

	mvwaddnstr(help->win, 0, 1, title, getmaxx(help->win) - 2);
	render_bindings(help, bindings, count, true);

	if (help->offset > 0)
		mvwaddch(help->win, 0, getmaxx(help->win) - 2, '^');

	if (help->offset + getmaxy(help->win) - 2 < help->lines)
		mvwaddch(help->win, getmaxy(help->win) - 1, getmaxx(help->win) - 2, 'v');

	if (top_panel(help->panel) == ERR)
		warnx("unable to raise help panel");
}
