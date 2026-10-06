// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <err.h>
#include <stdint.h>

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

	if (help->content && delwin(help->content) == ERR)
		warnx("unable to destroy help content");

	*help = (struct daemon_help) {
		.animation = help->animation,
		.duration_ms = help->duration_ms,
	};
}

static int animation_elapsed(struct daemon_help *help)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
		warn("unable to read help animation clock");
		return help->duration_ms;
	}

	int64_t elapsed = (int64_t) (now.tv_sec - help->started.tv_sec) * 1000 +
			  (now.tv_nsec - help->started.tv_nsec) / 1000000;

	return (int) CLAMP(elapsed, 0, help->duration_ms);
}

int daemon_help_timeout(struct daemon_help *help)
{
	if (!help->animating)
		return -1;

	int elapsed = animation_elapsed(help);

	return MAX(0, MIN(16 - (elapsed - help->frame_ms), help->duration_ms - elapsed));
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
		else {
			help->open = true;
			help->animating = help->animation == DAEMON_ANIMATION_SLIDE && help->duration_ms > 0;
			help->rows = LINES;
			help->columns = COLS;
			help->frame_ms = 0;

			if (help->animating && clock_gettime(CLOCK_MONOTONIC, &help->started) < 0) {
				warn("unable to start help animation");
				help->animating = false;
			}
		}

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
	int visible = width;

	if (help->animating && (help->rows != LINES || help->columns != COLS))
		help->animating = false;

	if (COLS < width || (help->open && (width < 4 || height < 3))) {
		release_window(help);
		help->animating = false;
		return false;
	}

	if (help->open) {
		if (help->content && (getmaxx(help->content) != width || getmaxy(help->content) != height)) {
			if (delwin(help->content) == ERR)
				warnx("unable to destroy help content");

			help->content = NULL;
		}

		if (!help->content) {
			help->content = newpad(height, width);

			if (!help->content) {
				warnx("unable to create help content");
				release_window(help);
				help->animating = false;
				return false;
			}
		}
	}

	if (help->animating) {
		help->frame_ms = animation_elapsed(help);
		visible = 1 + (width - 1) * help->frame_ms / help->duration_ms;

		if (help->frame_ms == help->duration_ms)
			help->animating = false;
	}

	if (help->win && (getmaxx(help->win) != visible || getmaxy(help->win) != height ||
			  getbegx(help->win) != COLS - visible || getbegy(help->win) != top))
		release_window(help);

	if (!help->win) {
		help->win = newwin(height, visible, top, COLS - visible);

		if (!help->win) {
			warnx("unable to create help window");
			help->animating = false;
			return false;
		}

		help->panel = new_panel(help->win);

		if (!help->panel) {
			warnx("unable to create help panel");
			release_window(help);
			help->animating = false;
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
	int width = getmaxx(help->content) - 2;
	int height = getmaxy(help->content) - 2;
	int key_width = 0;

	for (size_t i = 0; i < count; i++)
		key_width = MAX(key_width, (int) MIN(strlen(bindings[i].name), (size_t) width));

	/* Very narrow windows use wrapped labels instead of an empty text column. */
	if (key_width + 3 >= width)
		key_width = 0;

	int description_column = key_width ? key_width + 3 : 0;
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

					mvwaddch(help->content, y + 1, column + 1, cell);
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
	help->offset = MIN(help->offset, MAX(0, help->lines - (getmaxy(help->content) - 2)));

	struct widget style = { .win = help->content, .style_owner = focused };

	widget_style_apply_widget(&style, COLOR_PAIR_WINDOW);
	werase(help->content);
	box(help->content, 0, 0);

	char title[64];

	snprintf(title, sizeof(title), " Keys: %s ", widget_type((struct widget *) focused));

	mvwaddnstr(help->content, 0, 1, title, getmaxx(help->content) - 2);
	render_bindings(help, bindings, count, true);

	if (help->offset > 0)
		mvwaddch(help->content, 0, getmaxx(help->content) - 2, '^');

	if (help->offset + getmaxy(help->content) - 2 < help->lines)
		mvwaddch(help->content, getmaxy(help->content) - 1, getmaxx(help->content) - 2, 'v');

	if (copywin(help->content, help->win, 0, 0, 0, 0,
		    getmaxy(help->win) - 1, getmaxx(help->win) - 1, false) == ERR)
		warnx("unable to copy help content");

	if (top_panel(help->panel) == ERR)
		warnx("unable to raise help panel");
}
