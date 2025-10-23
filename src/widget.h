// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WIDGET_H_
#define _PLAINMOUTH_WIDGET_H_

#include <stdbool.h>

int widget_round(float number);
void widget_begin_yx(int width, bool border, int *begin_y, int *begin_x);

#endif /* _PLAINMOUTH_WIDGET_H_ */
