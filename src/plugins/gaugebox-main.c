// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include "macros.h"
#include "plugin.h"
#include "plugin_helpers.h"
#include "widget.h"

static void gauge_layout(struct widget *root, int x, int y, int width, int height)
{
	struct widget *label = find_widget_by_type_and_id(root, WIDGET_LABEL, 1);
	int margin = 0;
	if (label->parent != root)
		margin = 2;
	widget_measure_tree(root);
	/* Reserve the last content row for progress, clipping oversized prompts. */
	label->min_h = MIN(label->min_h, height - margin - 1);
	label->min_w = MIN(label->min_w, width - margin);
	for (struct widget *w = label->parent; w; w = w->parent)
		if (w->ops->measure)
			w->ops->measure(w);
	widget_layout_tree(root, x, y, width, height);
}

static struct widget *gauge_create(struct request *req)
{
	int width, height, value;
	if (!req_read_int(req, "width", &width) || !req_read_int(req, "height", &height) ||
	    !req_read_int(req, "value", &value))
		return NULL;
	if (width < 3 || height < 4 || value < 0 || value > 100) {
		req_error(req, "invalid gauge dimensions or percentage");
		return NULL;
	}
	wchar_t *text __free(ptr) = req_get_wchars(req, "text");
	if (!text)
		return NULL;
	struct widget *parent;
	struct widget *root = plugin_create_window(req, PLUGIN_WINDOW_VERTICAL, &parent);
	if (!root)
		return NULL;
	struct widget *label = make_label(text);
	if (!label)
		goto fail;
	label->w_id = 1;
	label->flex_h = 1;
	label->stretch_w = label->stretch_h = true;
	widget_add(parent, label);

	if (!widget_set_node_id(label, "text"))
		goto fail;

	struct widget *meter = make_meter(100);
	if (!meter)
		goto fail;
	meter->w_id = 2;
	widget_add(parent, meter);

	if (!widget_set_node_id(meter, "meter"))
		goto fail;

	if (!widget_set(meter, PROP_METER_VALUE, &value))
		goto fail;
	int x = req_get_int(req, "x", -1), y = req_get_int(req, "y", -1);
	position_center(width, height, &y, &x);
	gauge_layout(root, x, y, width, height);
	widget_render_tree(root);
	return root;
fail:
	widget_free(root);
	return NULL;
}

static enum p_retcode gauge_update(struct request *req, struct widget *root)
{
	int value;
	if (!req_read_int(req, "value", &value))
		return P_RET_ERR;
	if (value < 0 || value > 100) {
		req_error(req, "invalid gauge percentage");
		return P_RET_ERR;
	}
	if (req_get_val(req, "text")) {
		wchar_t *text __free(ptr) = req_get_wchars(req, "text");
		struct widget *label = find_widget_by_type_and_id(root, WIDGET_LABEL, 1);
		if (!text || !widget_set(label, PROP_TEXT_VALUE, text))
			return P_RET_ERR;
		gauge_layout(root, root->lx, root->ly, root->w, root->h);
	}
	struct widget *meter = find_widget_by_type_and_id(root, WIDGET_METER, 2);
	return widget_set(meter, PROP_METER_VALUE, &value) ? P_RET_OK : P_RET_ERR;
}

static bool gauge_finished(struct widget *root)
{
	(void) root;
	return false;
}

PLUGIN_EXPORT
struct plugin plugin = {
	.name = "gaugebox",
	.desc = "Display externally updated progress until deleted.",
	.p_create_instance = gauge_create,
	.p_update_instance = gauge_update,
	.p_set_value_instance = gauge_update,
	.p_finished = gauge_finished,
};
