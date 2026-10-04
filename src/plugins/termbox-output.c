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
	short pairs[64];
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

static short screen_pair(struct termbox_screen *st, short fg, short bg)
{
	if (fg < 0 || fg >= 8 || bg < 0 || bg >= 8)
		return COLOR_PAIR_WINDOW;

	int index = fg * 8 + bg;
	if (!st->pairs[index]) {
		short pair = widget_color_pair_alloc();
		if (pair < 0)
			return COLOR_PAIR_WINDOW;
		if (init_pair(pair, fg, bg) == ERR) {
			widget_color_pair_free(pair);
			return COLOR_PAIR_WINDOW;
		}
		st->pairs[index] = pair;
	}
	return st->pairs[index];
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
				pair = screen_pair(st, fg, bg);
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
	for (size_t i = 0; i < sizeof(st->pairs) / sizeof(st->pairs[0]); i++)
		widget_color_pair_free(st->pairs[i]);
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

int termbox_output_key(struct widget *view, wchar_t key, bool keycode)
{
	struct termbox_screen *st = view->state;
	VTermKey terminal_key = VTERM_KEY_NONE;

	if (keycode) {
		switch (key) {
			case KEY_ENTER:
				terminal_key = VTERM_KEY_ENTER;
				break;
			case KEY_UP:
				terminal_key = VTERM_KEY_UP;
				break;
			case KEY_DOWN:
				terminal_key = VTERM_KEY_DOWN;
				break;
			case KEY_LEFT:
				terminal_key = VTERM_KEY_LEFT;
				break;
			case KEY_RIGHT:
				terminal_key = VTERM_KEY_RIGHT;
				break;
			case KEY_IC:
				terminal_key = VTERM_KEY_INS;
				break;
			case KEY_DC:
				terminal_key = VTERM_KEY_DEL;
				break;
			case KEY_HOME:
				terminal_key = VTERM_KEY_HOME;
				break;
			case KEY_END:
				terminal_key = VTERM_KEY_END;
				break;
			case KEY_PPAGE:
				terminal_key = VTERM_KEY_PAGEUP;
				break;
			case KEY_NPAGE:
				terminal_key = VTERM_KEY_PAGEDOWN;
				break;
			default:
				if (key >= KEY_F(1) && key <= KEY_F(63))
					terminal_key = VTERM_KEY_FUNCTION(key - KEY_F(0));
				else
					return 0;
		}
	} else if (key == L'\n' || key == L'\r') {
		terminal_key = VTERM_KEY_ENTER;
	} else if (key == 27) {
		terminal_key = VTERM_KEY_ESCAPE;
	} else if (key < 0 || key > 31 || key == L'\t' || key == L'\b') {
		return 0;
	}

	if (!st->terminal || !st->active || st->failed)
		return -1;

	if (terminal_key != VTERM_KEY_NONE)
		vterm_keyboard_key(st->terminal, terminal_key, VTERM_MOD_NONE);
	else if (key >= 1 && key <= 26)
		vterm_keyboard_unichar(st->terminal, (uint32_t) key + 'a' - 1, VTERM_MOD_CTRL);
	else
		vterm_keyboard_unichar(st->terminal, (uint32_t) key + '@', VTERM_MOD_CTRL);

	/* Queue exhaustion rejects this key without poisoning output parsing. */
	if (st->failed) {
		st->failed = false;
		return -1;
	}
	return 1;
}
