// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <string.h>

#include "termbox-output.h"

enum escape_state {
	TEXT,
	ESCAPE,
	CSI,
	OSC,
	STRING,
	OSC_ESCAPE,
	STRING_ESCAPE,
};

static void output_char(struct termbox_output *out, wchar_t ch)
{
	if (out->cursor == out->length && out->length == TERMBOX_TEXT_LIMIT) {
		size_t drop = TERMBOX_TEXT_LIMIT / 8;
		wmemmove(out->text, out->text + drop, out->length - drop);
		out->length -= drop;
		out->line_start = out->line_start > drop ? out->line_start - drop : 0;
		out->cursor -= drop;
	}
	out->text[out->cursor++] = ch;
	if (out->cursor > out->length)
		out->length = out->cursor;
	out->text[out->length] = L'\0';
}

void termbox_output_finish(struct termbox_output *out)
{
	if (!mbsinit(&out->multibyte)) {
		memset(&out->multibyte, 0, sizeof(out->multibyte));
		output_char(out, L'?');
	}
}

static void output_byte(struct termbox_output *out, unsigned char byte)
{
	if (byte < 0x20 || byte == 0x7f) {
		termbox_output_finish(out);
		switch (byte) {
			case '\r':
				out->cursor = out->line_start;
				break;
			case '\n':
				out->cursor = out->length;
				output_char(out, L'\n');
				out->line_start = out->cursor;
				break;
			case '\b':
				if (out->cursor > out->line_start)
					out->cursor--;
				break;
			case '\t': {
				size_t columns = 0;
				for (size_t i = out->line_start; i < out->cursor; i++) {
					int width = wcwidth(out->text[i]);
					if (width > 0)
						columns += (size_t) width;
				}
				size_t spaces = 8 - columns % 8;
				while (spaces--)
					output_char(out, L' ');
				break;
			}
		}
		return;
	}
	wchar_t ch;
	char input = (char) byte;
	size_t result = mbrtowc(&ch, &input, 1, &out->multibyte);
	if (result == (size_t) -2)
		return;
	if (result == (size_t) -1) {
		memset(&out->multibyte, 0, sizeof(out->multibyte));
		output_char(out, L'?');
		if (byte < 0x80)
			output_char(out, (wchar_t) byte);
		return;
	}
	output_char(out, ch);
}

void termbox_output_feed(struct termbox_output *out, const char *data, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		unsigned char ch = (unsigned char) data[i];
		switch (out->escape_state) {
			case TEXT:
				if (ch == 0x1b) {
					termbox_output_finish(out);
					out->escape_state = ESCAPE;
				} else {
					output_byte(out, ch);
				}
				break;
			case ESCAPE:
				if (ch == '[')
					out->escape_state = CSI;
				else if (ch == ']')
					out->escape_state = OSC;
				else if (ch == 'P' || ch == 'X' || ch == '^' || ch == '_')
					out->escape_state = STRING;
				else if (ch >= 0x30 && ch <= 0x7e)
					out->escape_state = TEXT;
				break;
			case CSI:
				if (ch >= 0x40 && ch <= 0x7e)
					out->escape_state = TEXT;
				else if (ch == 0x1b)
					out->escape_state = ESCAPE;
				break;
			case OSC:
			case STRING:
				if (ch == 0x07 && out->escape_state == OSC)
					out->escape_state = TEXT;
				else if (ch == 0x1b)
					out->escape_state = out->escape_state == OSC ? OSC_ESCAPE : STRING_ESCAPE;
				break;
			case OSC_ESCAPE:
			case STRING_ESCAPE:
				if (ch == '\\' || (ch == 0x07 && out->escape_state == OSC_ESCAPE))
					out->escape_state = TEXT;
				else if (ch != 0x1b)
					out->escape_state = out->escape_state == OSC_ESCAPE ? OSC : STRING;
				break;
		}
	}
}
