// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <langinfo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>

#include "widget.h"

static const wchar_t *const sequences[] = {
	[SPINNER_ASCII] = L"|/-\\",
	[SPINNER_BRAILLE] = L"\u280b\u2819\u2839\u2838\u283c\u2834\u2826\u2827\u2807\u280f",
	[SPINNER_WAVE] = L"\u2582\u2583\u2584\u2585\u2586\u2587\u2588\u2587\u2586\u2585\u2584\u2583\u2582",
};

struct widget_spinner {
	enum widget_spinner_frames frames;
	size_t length, frame;
	bool active;
};

static bool single_column(const wchar_t *sequence)
{
	for (const wchar_t *p = sequence; *p; p++) {
		if (wcwidth(*p) != 1)
			return false;
	}

	return true;
}

static void spinner_measure(struct widget *w)
{
	w->min_w = 1;
	w->min_h = 1;
}

static void spinner_render(struct widget *w)
{
	struct widget_spinner *st = w->state;

	if (st->active) {
		wmove(w->win, 0, 0);
		w_addch(w->win, sequences[st->frames][st->frame]);
	}
}

static bool spinner_setter(struct widget *w, enum widget_property prop, const void *value)
{
	if (prop != PROP_SPINNER_ACTIVE)
		return false;

	struct widget_spinner *st = w->state;
	bool active = *(const bool *) value;

	if (st->active != active)
		st->frame = 0;

	st->active = active;
	return true;
}

static bool spinner_getter(struct widget *w, enum widget_property prop, void *value)
{
	struct widget_spinner *st = w->state;

	switch (prop) {
		case PROP_SPINNER_ACTIVE:
			*(bool *) value = st->active;
			return true;
		case PROP_SPINNER_FRAME:
			*(wchar_t *) value = L' ';

			if (st->active)
				*(wchar_t *) value = sequences[st->frames][st->frame];

			return true;
		case PROP_SPINNER_FRAMES:
			*(enum widget_spinner_frames *) value = st->frames;
			return true;
		default:
			return false;
	}
}

static void spinner_free(struct widget *w)
{
	free(w->state);
}

static const struct widget_ops spinner_ops = {
	.measure = spinner_measure,
	.render = spinner_render,
	.setter = spinner_setter,
	.getter = spinner_getter,
	.free = spinner_free,
};

bool widget_spinner_advance(struct widget *w, uint64_t ticks)
{
	if (!w || w->type != WIDGET_SPINNER)
		return false;

	struct widget_spinner *st = w->state;

	if (!st->active || !ticks)
		return false;

	st->frame = (st->frame + (size_t) (ticks % st->length)) % st->length;
	return true;
}

struct widget *make_spinner(enum widget_spinner_frames frames, bool active)
{
	if (frames < SPINNER_AUTO || frames > SPINNER_WAVE)
		return NULL;

	if (frames == SPINNER_AUTO) {
		const char *codeset = nl_langinfo(CODESET);
		const char *term = getenv("TERM");
		frames = SPINNER_ASCII;

		if ((!strcasecmp(codeset, "UTF-8") || !strcasecmp(codeset, "UTF8")) &&
		    (!term || strcmp(term, "linux")) && single_column(sequences[SPINNER_BRAILLE]))
			frames = SPINNER_BRAILLE;
	}

	if (!single_column(sequences[frames]))
		return NULL;

	struct widget *w = widget_create(WIDGET_SPINNER);

	if (!w)
		return NULL;

	w->ops = &spinner_ops;
	w->state = calloc(1, sizeof(struct widget_spinner));

	if (!w->state) {
		widget_free(w);
		return NULL;
	}

	struct widget_spinner *st = w->state;
	st->frames = frames;
	st->length = wcslen(sequences[frames]);
	st->active = active;
	w->color_pair = COLOR_PAIR_WINDOW;
	return w;
}
