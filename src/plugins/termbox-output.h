// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAINMOUTH_TERMBOX_OUTPUT_H
#define PLAINMOUTH_TERMBOX_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

struct widget;

#define TERMBOX_SCREEN_LIMIT 65536

typedef bool (*termbox_reply_fn)(void *, const char *, size_t);

struct widget *make_termbox_output(void);
bool termbox_output_start(struct widget *view, termbox_reply_fn reply, void *data);
bool termbox_output_feed(struct widget *view, const char *data, size_t length);
int termbox_output_key(struct widget *view, wchar_t key, bool keycode);
void termbox_output_finish(struct widget *view);

#endif
