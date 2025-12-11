// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "warray.h"

wchar_t *wcsndup(const wchar_t *s, size_t n)
{
	wchar_t *t = calloc((n + 1), sizeof(wchar_t));
	if (t) {
		wmemcpy(t, s, n);
		t[n] = L'\0';
	}
	return t;
}

static int warray_reserve(struct warray *a, size_t newcap)
{
	if (newcap <= a->capacity)
		return 0;

	wchar_t **p = realloc(a->data, newcap * sizeof(*p));
	if (!p)
		return -1;

	a->data = p;
	a->capacity = newcap;

	return 0;
}

int warray_init(struct warray *a)
{
	a->data = NULL;
	a->size = 0;
	a->capacity = 0;

	return 0;
}

void warray_free(struct warray *a)
{
	if (!a)
		return;

	for (size_t i = 0; i < a->size; i++)
		free(a->data[i]);

	free(a->data);

	a->data = NULL;
	a->size = 0;
	a->capacity = 0;
}

int warray_push(struct warray *a, const wchar_t *s, size_t len)
{
	wchar_t *copy = NULL;

	if (s) {
		copy = (len > 0) ? wcsndup(s, len) : wcsdup(s);
		if (!copy)
			return -1;
	}

	if (a->size == a->capacity) {
		size_t newcap = a->capacity ? a->capacity * 2 : 8;

		if (warray_reserve(a, newcap) < 0) {
			free(copy);
			return -1;
		}
	}

	a->data[a->size++] = copy;

	return 0;
}

int warray_insert(struct warray *a, size_t index, const wchar_t *s, size_t len)
{
	if (index > a->size)
		return -1;

	wchar_t *copy = NULL;

	if (s) {
		copy = (len > 0) ? wcsndup(s, len) : wcsdup(s);
		if (!copy)
			return -1;
	}

	if (a->size == a->capacity) {
		size_t newcap = a->capacity ? a->capacity * 2 : 8;

		if (warray_reserve(a, newcap) < 0) {
			free(copy);
			return -1;
		}
	}

	memmove(&a->data[index + 1], &a->data[index], (a->size - index) * sizeof(wchar_t *));

	a->data[index] = copy;
	a->size++;

	return 0;
}

int warray_remove(struct warray *a, size_t index)
{
	if (index >= a->size)
		return -1;

	free(a->data[index]);

	memmove(&a->data[index], &a->data[index + 1], (a->size - index - 1) * sizeof(wchar_t *));

	a->size--;
	return 0;
}

const wchar_t *warray_get(const struct warray *a, size_t index)
{
	if (index >= a->size)
		return NULL;
	return a->data[index];
}
