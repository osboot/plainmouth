// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_WARRAY_H_
#define _PLAINMOUTH_WARRAY_H_

#include <stdlib.h>

wchar_t *wcsndup(const wchar_t *s, size_t n);

struct warray {
	wchar_t **data;
	size_t size;
	size_t capacity;
};

int warray_init(struct warray *a);
void warray_free(struct warray *a);
int warray_push(struct warray *a, const wchar_t *s, size_t len);
int warray_insert(struct warray *a, size_t index, const wchar_t *s, size_t len);
int warray_remove(struct warray *a, size_t index);
const wchar_t *warray_get(const struct warray *a, size_t index);

#endif // _PLAINMOUTH_WARRAY_H_
