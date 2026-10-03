// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_TERMBOX_OUTPUT_H
#define PLAINMOUTH_TERMBOX_OUTPUT_H

#include <stddef.h>
#include <wchar.h>

#define TERMBOX_TEXT_LIMIT 65536

/* A bounded text transcript, not a terminal screen model. */
struct termbox_output {
	wchar_t text[TERMBOX_TEXT_LIMIT + 1];
	size_t length, line_start, cursor;
	mbstate_t multibyte;
	int escape_state;
};

void termbox_output_feed(struct termbox_output *out, const char *data, size_t length);
void termbox_output_finish(struct termbox_output *out);

#endif
