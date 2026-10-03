// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "plugins/termbox-output.h"

static void feed(struct termbox_output *out, const char *text)
{
	termbox_output_feed(out, text, strlen(text));
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	struct termbox_output *out = calloc(1, sizeof(*out));
	assert(out);
	feed(out, "progress 10%\rprogress 90%\r\n");
	feed(out, "abc\bX\r\n");
	assert(!wcscmp(out->text, L"progress 90%\nabX\n"));
	feed(out, "\033[31");
	feed(out, "mred\033[0m\033]0;hidden");
	feed(out, "\007\033Pignored\033\\ plain\n");
	assert(wcsstr(out->text, L"red plain\n"));
	assert(!wcsstr(out->text, L"hidden"));
	feed(out, "\303");
	assert(out->text[out->length - 1] == L'\n');
	feed(out, "\251\n");
	assert(out->text[out->length - 2] == L'\u00e9');
	feed(out, "\xff\n");
	assert(out->text[out->length - 2] == L'?');
	feed(out, "\303");
	termbox_output_finish(out);
	assert(out->text[out->length - 1] == L'?');
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < TERMBOX_TEXT_LIMIT * 2; i++)
		feed(out, "x");
	feed(out, "\nfinal\n");
	assert(out->length <= TERMBOX_TEXT_LIMIT);
	assert(out->cursor <= out->length);
	assert(out->line_start <= out->length);
	assert(wcsstr(out->text, L"final\n"));
	feed(out, "a\tb");
	assert(wcsstr(out->text, L"a       b"));
	free(out);
	return 0;
}
