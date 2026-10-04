// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <sys/queue.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

#include "daemon_style.h"
#include "macros.h"
#include "request.h"
#include "widget.h"

struct named_style {
	TAILQ_ENTRY(named_style)
	entries;
	char *name;
	struct widget *source;
};
TAILQ_HEAD(named_styles, named_style);
static struct named_styles named_styles = TAILQ_HEAD_INITIALIZER(named_styles);

static struct named_style *find_named_style(const char *name)
{
	struct named_style *style;
	TAILQ_FOREACH(style, &named_styles, entries)
	{
		if (streq(style->name, name))
			return style;
	}
	return NULL;
}

static void free_named_style(struct named_style *style)
{
	widget_free(style->source);
	free(style->name);
	free(style);
}

static bool convert_color(struct request *req, const char *color, int *cnum)
{
	static const char *builtin_colors[8] = {
		[COLOR_BLACK]   = "black",
		[COLOR_RED]     = "red",
		[COLOR_GREEN]   = "green",
		[COLOR_YELLOW]  = "yellow",
		[COLOR_BLUE]    = "blue",
		[COLOR_MAGENTA] = "magenta",
		[COLOR_CYAN]    = "cyan",
		[COLOR_WHITE]   = "white",
	};
	int num;

	if (!color) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=missing color name",
				req_id(req));
		return false;
	}

	for (num = 0; num < 8; num++)
		if (builtin_colors[num] && streq(color, builtin_colors[num]))
			goto has_number;

	if (streq(color, "default")) {
		num = -1;
		goto has_number;
	}

	if (strlen(color) > 5 && strneq("color", color, 5)) {
		char *end;
		errno = 0;
		long value = strtol(color + 5, &end, 10);
		if (errno || *end || value < 0 || value >= COLORS ||
		    color[5] < '0' || color[5] > '9') {
			req_error(req, "invalid color number: %s", color);
			return false;
		}
		num = (int) value;
		goto has_number;
	}

	ipc_send_string(req_fd(req), "RESPDATA %s ERR=unknown color name: %s",
			req_id(req), color);
	return false;

has_number:
	if (num >= COLORS) {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=color out of range: %s",
				req_id(req), color);
		return false;
	}

	*cnum = num;
	return true;
}

static bool parse_style_attrs(struct request *req, const char *text, attr_t *attrs)
{
	static const struct {
		const char *name;
		attr_t value;
	} names[] = {
		{ "bold",      A_BOLD      },
		{ "dim",       A_DIM       },
		{ "underline", A_UNDERLINE },
		{ "reverse",   A_REVERSE   },
		{ "blink",     A_BLINK     },
		{ "italic",    A_ITALIC    },
	};

	*attrs = A_NORMAL;
	if (streq(text, "normal"))
		return true;

	char *copy = strdup(text);
	if (!copy) {
		req_error(req, "unable to allocate style attributes");
		return false;
	}

	bool valid = true;
	char *remaining = copy, *token;
	while ((token = strsep(&remaining, ","))) {
		bool found = false;
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (streq(token, names[i].name)) {
				*attrs |= names[i].value;
				found = true;
				break;
			}
		}
		if (!found) {
			req_error(req, "unknown style attribute: %s", token);
			valid = false;
			break;
		}
	}
	free(copy);
	return valid;
}

bool daemon_style_apply(struct request *req, struct widget *(*lookup_instance)(void *),
			void *data, struct widget **changed)
{
	const char *name, *fg_name, *bg_name;
	int pair, fg, bg;
	attr_t attrs = A_NORMAL;
	const char *attrs_name = req_get_val(req, "attrs");
	const char *id = req_get_val(req, "id");
	const char *style_name = req_get_val(req, "style");
	bool reset = false;
	if (id && style_name) {
		req_error(req, "id and style cannot be combined");
		return false;
	}
	if (style_name && !*style_name) {
		req_error(req, "style name cannot be empty");
		return false;
	}

	name = req_get_val(req, "name");
	fg_name = req_get_val(req, "fg");
	bg_name = req_get_val(req, "bg");

	if (streq(name, "main"))
		pair = COLOR_PAIR_MAIN;
	else if (streq(name, "window"))
		pair = COLOR_PAIR_WINDOW;
	else if (streq(name, "button"))
		pair = COLOR_PAIR_BUTTON;
	else if (streq(name, "focus"))
		pair = COLOR_PAIR_FOCUS;
	else {
		ipc_send_string(req_fd(req), "RESPDATA %s ERR=unknown style: %s",
				req_id(req), name);
		return false;
	}

	if (attrs_name && !parse_style_attrs(req, attrs_name, &attrs))
		return false;

	if (!req_read_bool(req, "reset", false, &reset))
		return false;
	if (req_get_val(req, "reset") && !id && !style_name) {
		req_error(req, "style reset requires an instance id or style name");
		return false;
	}
	if (reset && (fg_name || bg_name || attrs_name)) {
		req_error(req, "style reset cannot be combined with overrides");
		return false;
	}
	if (!fg_name && !bg_name && !attrs_name && !reset) {
		req_error(req, "missing style colors or attributes");
		return false;
	}

	if (id || style_name) {
		struct widget *instance = NULL;
		struct named_style *style = NULL;
		bool new_style = false;
		if (id) {
			instance = lookup_instance(data);
			if (!instance)
				return false;
		} else {
			style = find_named_style(style_name);
			if (!style && reset) {
				req_error(req, "unknown style: %s", style_name);
				return false;
			}
		}
		if (pair == COLOR_PAIR_MAIN) {
			req_error(req, "main style is global only");
			return false;
		}
		const int *fg_value = NULL, *bg_value = NULL;
		const attr_t *attrs_value = NULL;
		if (fg_name) {
			if (!convert_color(req, fg_name, &fg))
				return false;
			fg_value = &fg;
		}
		if (bg_name) {
			if (!convert_color(req, bg_name, &bg))
				return false;
			bg_value = &bg;
		}
		if (attrs_name)
			attrs_value = &attrs;
		struct widget *target;
		if (instance) {
			target = instance;
		} else {
			if (!style) {
				style = calloc(1, sizeof(*style));
				if (!style) {
					req_error(req, "unable to allocate named style");
					return false;
				}
				style->name = strdup(style_name);
				style->source = widget_create(WIDGET_WINDOW);
				if (!style->name || !style->source) {
					free_named_style(style);
					req_error(req, "unable to allocate named style");
					return false;
				}
				new_style = true;
			}
			target = style->source;
		}
		if (!widget_style_override(target, pair, fg_value, bg_value, attrs_value, reset)) {
			if (new_style)
				free_named_style(style);
			req_error(req, "unable to update style");
			return false;
		}
		if (new_style) {
			TAILQ_INSERT_HEAD(&named_styles, style, entries);
		}
		*changed = target;
		return true;
	}

	if (fg_name || bg_name) {
		if (!convert_color(req, fg_name, &fg) ||
		    !convert_color(req, bg_name, &bg))
			return false;
		if (init_extended_pair(pair, fg, bg) == ERR) {
			req_error(req, "unable to update color pair");
			return false;
		}
	}
	if (attrs_name)
		widget_style_set_attrs(pair, attrs);

	*changed = NULL;
	return true;
}

struct widget *daemon_style_find(const char *name)
{
	struct named_style *style = find_named_style(name);
	return style ? style->source : NULL;
}

void daemon_styles_free(void)
{
	while (!TAILQ_EMPTY(&named_styles)) {
		struct named_style *style = TAILQ_FIRST(&named_styles);
		TAILQ_REMOVE(&named_styles, style, entries);
		free_named_style(style);
	}
}
