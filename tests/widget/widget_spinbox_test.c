// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <limits.h>

#include <curses.h>

#include "widget.h"

static void expect(struct widget *w, int value)
{
	int actual;
	assert(widget_get(w, PROP_SPINBOX_VALUE, &actual));
	assert(actual == value);
}

int main(void)
{
	struct widget *w = make_spinbox(INT_MIN, INT_MAX, 1, INT_MAX, 10);
	assert(w);
	assert(w->ops->input(w, KEY_UP));
	expect(w, INT_MAX);
	int value = INT_MIN;
	assert(widget_set(w, PROP_SPINBOX_VALUE, &value));
	assert(w->ops->input(w, KEY_DOWN));
	expect(w, INT_MIN);
	assert(w->ops->input(w, KEY_UP));
	expect(w, INT_MIN + 1);
	for (int i = 0; i < 10; i++)
		assert(w->ops->input(w, L'9'));
	expect(w, INT_MAX);
	widget_free(w);
	return 0;
}
