// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>

#include "widget.h"

static wchar_t frame(struct widget *w)
{
	wchar_t value;
	assert(widget_get(w, PROP_SPINNER_FRAME, &value));
	return value;
}

static void test_sequence(enum widget_spinner_frames frames, const wchar_t *sequence)
{
	struct widget *w = make_spinner(frames, true);
	assert(w);
	widget_measure_tree(w);
	assert(w->min_w == 1 && w->min_h == 1);
	assert(!(w->attrs & (ATTR_CAN_FOCUS | ATTR_CAN_CURSOR)));

	for (const wchar_t *p = sequence; *p; p++) {
		assert(frame(w) == *p);
		assert(widget_spinner_advance(w, 1));
	}

	assert(frame(w) == *sequence);
	assert(!widget_spinner_advance(w, 0));
	assert(widget_spinner_advance(w, UINT64_MAX));
	assert(frame(w) == sequence[UINT64_MAX % wcslen(sequence)]);
	bool active = false;
	assert(widget_set(w, PROP_SPINNER_ACTIVE, &active));
	assert(frame(w) == L' ');
	assert(!widget_spinner_advance(w, 1));
	widget_measure_tree(w);
	assert(w->min_w == 1 && w->min_h == 1);
	active = true;
	assert(widget_set(w, PROP_SPINNER_ACTIVE, &active));
	assert(frame(w) == *sequence);
	assert(widget_spinner_advance(w, 1));
	assert(widget_set(w, PROP_SPINNER_ACTIVE, &active));
	assert(frame(w) == sequence[1]);
	widget_free(w);
}

static void expect_auto(enum widget_spinner_frames expected)
{
	struct widget *w = make_spinner(SPINNER_AUTO, false);
	assert(w);
	enum widget_spinner_frames frames;
	assert(widget_get(w, PROP_SPINNER_FRAMES, &frames));
	assert(frames == expected);
	widget_free(w);
}

int main(void)
{
	assert(setlocale(LC_CTYPE, "C"));
	assert(setenv("TERM", "xterm", 1) == 0);
	expect_auto(SPINNER_ASCII);
	test_sequence(SPINNER_ASCII, L"|/-\\");
	assert(!make_spinner(SPINNER_BRAILLE, true));
	assert(!make_spinner(SPINNER_WAVE, true));
	assert(!make_spinner((enum widget_spinner_frames) - 1, true));
	assert(!make_spinner((enum widget_spinner_frames) 99, true));
	assert(!widget_spinner_advance(NULL, 1));
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	expect_auto(SPINNER_BRAILLE);
	test_sequence(SPINNER_BRAILLE, L"\u280b\u2819\u2839\u2838\u283c\u2834\u2826\u2827\u2807\u280f");
	test_sequence(SPINNER_WAVE, L"\u2582\u2583\u2584\u2585\u2586\u2587\u2588\u2587\u2586\u2585\u2584\u2583\u2582");
	assert(setenv("TERM", "linux", 1) == 0);
	expect_auto(SPINNER_ASCII);
	test_sequence(SPINNER_BRAILLE, L"\u280b\u2819\u2839\u2838\u283c\u2834\u2826\u2827\u2807\u280f");
	return 0;
}
