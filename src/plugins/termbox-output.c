// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <wchar.h>

#include <vterm.h>

#include "widget.h"
#include "termbox-output.h"

struct termbox_screen {
	VTerm *terminal;
	VTermScreen *screen;
	VTermPos cursor;
	int rows, cols;
	bool active, visible, reverse, failed;
	termbox_reply_fn reply;
	void *reply_data;
};

static int screen_cursor(VTermPos pos, VTermPos oldpos, int visible, void *data)
{
	(void) oldpos;
	struct termbox_screen *st = data;

	st->cursor = pos;
	st->visible = visible != 0;

	return 1;
}

static int screen_property(VTermProp prop, VTermValue *value, void *data)
{
	struct termbox_screen *st = data;

	if (prop == VTERM_PROP_CURSORVISIBLE)
		st->visible = value->boolean != 0;

	else if (prop == VTERM_PROP_REVERSE)
		st->reverse = value->boolean != 0;

	return 1;
}

static void screen_reply(const char *bytes, size_t length, void *data)
{
	struct termbox_screen *st = data;

	if (st->reply && !st->reply(st->reply_data, bytes, length))
		st->failed = true;
}

static const VTermScreenCallbacks screen_callbacks = {
	.movecursor = screen_cursor,
	.settermprop = screen_property,
};

/* Approximate extended colors with the terminal's eight basic colors. */
static short screen_color(VTermScreen *screen, const VTermColor *color, short fallback)
{
	if (color->type & VTERM_COLOR_DEFAULT_MASK)
		return fallback;

	if (VTERM_COLOR_IS_INDEXED(color) && color->indexed.idx < 16)
		return (short) (color->indexed.idx % 8);

	VTermColor rgb = *color;

	vterm_screen_convert_color_to_rgb(screen, &rgb);

	short best = fallback;
	long distance = LONG_MAX;

	for (short i = 0; i < 8 && i < COLORS; i++) {
		short red, green, blue;

		if (color_content(i, &red, &green, &blue) == ERR)
			continue;

		long r = rgb.rgb.red * 1000L / 255 - red;
		long g = rgb.rgb.green * 1000L / 255 - green;
		long b = rgb.rgb.blue * 1000L / 255 - blue;
		long current = r * r + g * g + b * b;

		if (current < distance) {
			distance = current;
			best = i;
		}
	}
	return best;
}

static short screen_pair(short fg, short bg)
{
	static bool initialized[64];
	int index = fg * 8 + bg;
	int pair = COLOR_PAIR_TERMINAL + index;

	if (fg < 0 || fg >= 8 || bg < 0 || bg >= 8 || pair >= COLOR_PAIRS)
		return COLOR_PAIR_WINDOW;

	if (!initialized[index]) {
		if (init_pair((short) pair, fg, bg) == ERR)
			return COLOR_PAIR_WINDOW;

		initialized[index] = true;
	}

	return (short) pair;
}

static void screen_render(struct widget *w)
{
	struct termbox_screen *st = w->state;

	w->attrs &= ~ATTR_CAN_CURSOR;

	wbkgd(w->win, COLOR_PAIR(w->color_pair));
	werase(w->win);

	if (!st->terminal)
		return;

	short default_fg = COLOR_WHITE, default_bg = COLOR_BLACK;

	if (has_colors())
		pair_content(w->color_pair, &default_fg, &default_bg);

	for (int y = 0; y < st->rows && y < w->h; y++) {
		for (int x = 0; x < st->cols && x < w->w; x++) {
			VTermScreenCell cell;
			VTermPos pos = { .row = y, .col = x };

			if (!vterm_screen_get_cell(st->screen, pos, &cell) || cell.chars[0] == UINT32_MAX)
				continue;

			wchar_t text[CCHARW_MAX] = { 0 };

			if (!cell.chars[0])
				text[0] = L' ';
			else
				for (int i = 0; i < CCHARW_MAX - 1 && i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++)
					text[i] = (wchar_t) cell.chars[i];

			attr_t attrs = A_NORMAL;

			if (cell.attrs.bold)
				attrs |= A_BOLD;

			if (cell.attrs.underline)
				attrs |= A_UNDERLINE;

			if (cell.attrs.italic)
				attrs |= A_ITALIC;

			if (cell.attrs.blink)
				attrs |= A_BLINK;

			if (!!cell.attrs.reverse != st->reverse)
				attrs |= A_REVERSE;

			if (cell.attrs.conceal)
				attrs |= A_INVIS;

			short pair = 0;

			if (has_colors()) {
				short fg = screen_color(st->screen, &cell.fg, default_fg);
				short bg = screen_color(st->screen, &cell.bg, default_bg);
				pair = screen_pair(fg, bg);
			}

			cchar_t character;

			if (setcchar(&character, text, attrs, pair, NULL) != ERR)
				mvwadd_wch(w->win, y, x, &character);
		}
	}

	if (st->active && st->visible &&
	    st->cursor.row >= 0 && st->cursor.row < w->h &&
	    st->cursor.col >= 0 && st->cursor.col < w->w &&
	    wmove(w->win, st->cursor.row, st->cursor.col) != ERR)
		w->attrs |= ATTR_CAN_CURSOR;
}

static void screen_free(struct widget *w)
{
	struct termbox_screen *st = w->state;
	if (st->terminal)
		vterm_free(st->terminal);
	free(st);
}

static const struct widget_ops screen_ops = {
	.render = screen_render,
	.free = screen_free,
};

struct widget *make_termbox_output(void)
{
	struct widget *w = widget_create(WIDGET_TERMINAL);
	if (!w)
		return NULL;

	struct termbox_screen *st = calloc(1, sizeof(*st));
	if (!st) {
		widget_free(w);
		return NULL;
	}

	w->state = st;
	w->ops = &screen_ops;
	w->min_w = w->min_h = 1;
	w->flex_w = w->flex_h = 1;
	w->stretch_w = w->stretch_h = true;
	w->attrs = ATTR_CAN_FOCUS;
	w->color_pair = COLOR_PAIR_WINDOW;

	return w;
}

bool termbox_output_start(struct widget *view, termbox_reply_fn reply, void *data)
{
	struct termbox_screen *st = view->state;

	if (st->terminal || view->w < 1 || view->h < 1 ||
	    (size_t) view->w > TERMBOX_SCREEN_LIMIT / (size_t) view->h)
		return false;

	st->terminal = vterm_new(view->h, view->w);
	if (!st->terminal)
		return false;

	st->screen = vterm_obtain_screen(st->terminal);
	if (!st->screen)
		return false;

	st->rows = view->h;
	st->cols = view->w;
	st->active = st->visible = true;
	st->reply = reply;
	st->reply_data = data;

	vterm_set_utf8(st->terminal, 1);
	vterm_output_set_callback(st->terminal, screen_reply, st);
	vterm_screen_set_callbacks(st->screen, &screen_callbacks, st);
	vterm_screen_set_damage_merge(st->screen, VTERM_DAMAGE_SCREEN);
	vterm_screen_enable_altscreen(st->screen, 1);
	vterm_screen_reset(st->screen, 1);

	return !st->failed;
}

bool termbox_output_feed(struct widget *view, const char *data, size_t length)
{
	struct termbox_screen *st = view->state;

	if (vterm_input_write(st->terminal, data, length) != length)
		return false;

	vterm_screen_flush_damage(st->screen);

	return !st->failed;
}

void termbox_output_finish(struct widget *view)
{
	struct termbox_screen *st = view->state;
	st->active = false;
}
