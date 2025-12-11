// SPDX-License-Identifier: GPL-2.0-or-later

#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <wchar.h>
#include <stdio.h>

#include "warray.h"

static void expect_eq(const struct warray *a, size_t idx, const wchar_t *expected)
{
	const wchar_t *s = warray_get(a, idx);
	assert(s != NULL);
	assert(wcscmp(s, expected) == 0);
}

static void expect_null(const struct warray *a, size_t idx)
{
	const wchar_t *s = warray_get(a, idx);
	assert(s == NULL);
}

static void test_init_free(void)
{
	struct warray a;

	assert(warray_init(&a) == 0);

	assert(a.size == 0);
	assert(a.capacity == 0);
	assert(a.data == NULL);

	warray_free(&a);

	assert(a.size == 0);
	assert(a.capacity == 0);
	assert(a.data == NULL);
}

static void test_push(void)
{
	struct warray a;
	warray_init(&a);

	assert(warray_push(&a, L"hello", 0) == 0);
	assert(a.size == 1);
	expect_eq(&a, 0, L"hello");

	assert(warray_push(&a, L"world", 0) == 0);
	assert(a.size == 2);
	expect_eq(&a, 1, L"world");

	warray_free(&a);
}

static void test_insert(void)
{
	struct warray a;
	warray_init(&a);

	warray_push(&a, L"one", 0);   /* idx 0 */
	warray_push(&a, L"three", 0); /* idx 1 */

	assert(warray_insert(&a, 1, L"two", 0) == 0);
	assert(a.size == 3);

	expect_eq(&a, 0, L"one");
	expect_eq(&a, 1, L"two");
	expect_eq(&a, 2, L"three");

	assert(warray_insert(&a, 0, L"zero", 0) == 0);
	assert(a.size == 4);

	expect_eq(&a, 0, L"zero");
	expect_eq(&a, 1, L"one");

	assert(warray_insert(&a, 4, L"four", 0) == 0);
	assert(a.size == 5);
	expect_eq(&a, 4, L"four");

	warray_free(&a);
}

static void test_remove(void)
{
	struct warray a;
	warray_init(&a);

	warray_push(&a, L"one", 0);
	warray_push(&a, L"two", 0);
	warray_push(&a, L"three", 0);
	warray_push(&a, L"four", 0);

	assert(warray_remove(&a, 1) == 0); /* remove "two" */
	assert(a.size == 3);

	expect_eq(&a, 0, L"one");
	expect_eq(&a, 1, L"three");
	expect_eq(&a, 2, L"four");

	assert(warray_remove(&a, 2) == 0); /* remove "four" */
	assert(a.size == 2);
	expect_eq(&a, 1, L"three");

	assert(warray_remove(&a, 0) == 0); /* remove "one" */
	assert(a.size == 1);
	expect_eq(&a, 0, L"three");

	assert(warray_remove(&a, 0) == 0);
	assert(a.size == 0);

	assert(warray_remove(&a, 0) == -1);

	warray_free(&a);
}

static void test_null_insert_and_push(void)
{
	struct warray a;
	warray_init(&a);

	assert(warray_push(&a, NULL, 0) == 0);
	assert(a.size == 1);
	expect_null(&a, 0);

	assert(warray_insert(&a, 0, NULL, 0) == 0);
	assert(a.size == 2);
	expect_null(&a, 0);
	expect_null(&a, 1);

	warray_free(&a);
}

static void test_large_growth(void)
{
	struct warray a;
	warray_init(&a);

	for (int i = 0; i < 1000; i++) {
		wchar_t buf[32];
		swprintf(buf, 32, L"%d", i);
		assert(warray_push(&a, buf, 0) == 0);
	}

	assert(a.size == 1000);
	expect_eq(&a, 0, L"0");
	expect_eq(&a, 999, L"999");

	warray_free(&a);
}

int main(void)
{
	test_init_free();
	test_push();
	test_insert();
	test_remove();
	test_null_insert_and_push();
	test_large_growth();

	return 0;
}
