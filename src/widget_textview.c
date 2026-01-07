// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include "widget.h"

struct widget *make_textview(const wchar_t *text)
{
	struct widget *scroll = make_scroll_vbox();
	struct widget *w = make_label(text);

	if (!scroll || !w)
		return NULL;

	widget_add(scroll, w);

	return scroll;
}
