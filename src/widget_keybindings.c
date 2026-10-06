// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include "widget.h"

static bool contains(const struct widget_keybinding *bindings, size_t count,
		     const struct widget_keybinding *candidate)
{
	for (size_t i = 0; i < count; i++)
		if (bindings[i].key == candidate->key && bindings[i].keycode == candidate->keycode)
			return true;
	return false;
}

size_t widget_keybindings(const struct widget *w, struct widget_keybinding *bindings, size_t capacity)
{
	if (!w || !widget_is_interactive(w) || w->type == WIDGET_TERMINAL)
		return 0;
	size_t count = 0;
	for (const struct widget *owner = w; owner; owner = owner->parent) {
		if (owner != w && owner->type != WIDGET_SCROLL_VBOX)
			continue;
		const struct widget_keybinding *local = NULL;
		size_t n = 0;
		if (owner->ops && owner->ops->keybindings)
			n = owner->ops->keybindings(owner, &local);
		for (size_t i = 0; i < n && count < capacity; i++) {
			if (owner != w && (!local[i].keycode ||
					   (local[i].key != KEY_UP && local[i].key != KEY_DOWN &&
					    local[i].key != KEY_PPAGE && local[i].key != KEY_NPAGE)))
				continue;
			if (!contains(bindings, count, &local[i]))
				bindings[count++] = local[i];
		}
		/* Input dispatch forwards only to the nearest scrolling parent. */
		if (owner != w)
			break;
	}
	return count;
}
