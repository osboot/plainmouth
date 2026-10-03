// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_TERMBOX_INPUT_H
#define PLAINMOUTH_TERMBOX_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#define TERMBOX_INPUT_LIMIT 65536

struct termbox_input {
	char data[TERMBOX_INPUT_LIMIT];
	size_t offset, length;
	unsigned char erase;
};

bool termbox_input_append(struct termbox_input *input, const char *text, size_t length);
bool termbox_input_append_bytes(struct termbox_input *input, const char *bytes, size_t length);
bool termbox_input_flush(struct termbox_input *input, int fd);

#endif
