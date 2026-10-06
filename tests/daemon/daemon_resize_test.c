// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <curses.h>
#include <panel.h>

#include "daemon_instance.h"
#include "plugin.h"
#include "widget.h"

static struct widget *create_dialog(struct request *req)
{
	(void) req;
	struct widget *root = make_window();
	struct widget *scroll = make_scroll_vbox();
	struct widget *canvas = make_positioned();
	struct widget *label = make_label(L"Name:");
	struct widget *input = make_input(L"preserved", NULL);
	assert(root && scroll && canvas && label && input);
	widget_add(root, scroll);
	widget_add(scroll, canvas);
	assert(positioned_add(canvas, label, 0, 0, 6, 1));
	assert(positioned_add(canvas, input, 7, 0, 10, 1));
	widget_measure_tree(root);
	widget_layout_tree(root, 20, 8, 40, 8);
	widget_render_tree(root);
	return root;
}

static struct plugin test_plugin = { .name = "resize-test", .p_create_instance = create_dialog };

struct plugin *find_plugin(const char *name)
{
	assert(strcmp(name, test_plugin.name) == 0);
	return &test_plugin;
}

static void resize_dialog(int rows, int cols)
{
	assert(resize_term(rows, cols) == OK);
	daemon_instances_resize();
}

int main(void)
{
	FILE *input = tmpfile(), *output = tmpfile();
	assert(input && output);
	SCREEN *screen = newterm("xterm", output, input);
	assert(screen);
	assert(resize_term(24, 80) == OK);
	struct ipc_ctx ctx = { .fd = -1 };
	char id[] = "test";
	struct ipc_message msg = { .id = id };
	struct request req = { .r_ctx = &ctx, .r_msg = &msg };
	assert(ipc_pair_add(&msg.data, "id", "dialog"));
	assert(ipc_pair_add(&msg.data, "plugin", test_plugin.name));
	assert(daemon_instance_create(&req));
	struct instance *ins = daemon_instance_find("dialog");
	struct widget *field = find_widget_by_type_and_id(ins->root, WIDGET_INPUT, 0);
	struct widget *pad = find_widget_by_type_and_id(ins->root, WIDGET_PAD_BOX, 0);
	struct widget *hscroll = find_widget_by_type_and_id(ins->root, WIDGET_HSCROLL, 0);
	assert(field && pad && hscroll);
	daemon_focus_next();
	assert(daemon_focus_get() == field);
	for (int repeat = 0; repeat < 3; repeat++) {
		resize_dialog(6, 12);
		assert(ins->root->w == 12 && ins->root->h == 6);
		assert(panel_window(ins->panel) == ins->root->win);
		assert(hscroll->h == 1 && hscroll->win);
		int x;
		assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 5);
		assert(daemon_focus_get() == field);
		resize_dialog(1, 1);
		assert(panel_hidden(ins->panel));
		assert(!(ins->root->flags & FLAG_VISIBLE));
		resize_dialog(30, 100);
		assert(!panel_hidden(ins->panel));
		assert(ins->root->w == 40 && ins->root->h == 8);
		assert(ins->root->lx == 30 && ins->root->ly == 11);
		assert(hscroll->h == 0);
		assert(widget_get(pad, PROP_SCROLL_X, &x) && x == 0);
		const wchar_t *value;
		assert(widget_get(field, PROP_INPUT_VALUE, &value));
		assert(wcscmp(value, L"preserved") == 0);
		int y;
		assert(widget_coordinates_yx(field, &y, &x));
		assert(y == 11 && x >= 37 && x < 47);
		assert(daemon_focus_get() == field);
	}
	daemon_instances_free();
	ipc_pair_free(&msg.data);
	endwin();
	delscreen(screen);
	fclose(input);
	fclose(output);
	return 0;
}
