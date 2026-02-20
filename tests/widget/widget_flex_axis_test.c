// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <assert.h>

#include "widget.h"

static void assert_out_eq(const int *out, const int *want, int n)
{
	for (int i = 0; i < n; i++)
		assert(out[i] == want[i]);
}

static void test_grow_with_remainder(void)
{
	const int pref[] = { 2, 2 };
	const int min[] = { 2, 2 };
	const int max[] = { 0, 0 };
	const int grow[] = { 1, 1 };
	const int shrink[] = { 1, 1 };
	const int want[] = { 4, 3 };
	int out[] = { 0, 0 };

	distribute_flex_axis(2, pref, min, max, grow, shrink, 7, out);
	assert_out_eq(out, want, 2);
}

static void test_grow_respects_max(void)
{
	const int pref[] = { 2, 2 };
	const int min[] = { 2, 2 };
	const int max[] = { 3, 0 };
	const int grow[] = { 1, 1 };
	const int shrink[] = { 1, 1 };
	const int want[] = { 3, 3 };
	int out[] = { 0, 0 };

	distribute_flex_axis(2, pref, min, max, grow, shrink, 6, out);
	assert_out_eq(out, want, 2);
}

static void test_shrink_proportional(void)
{
	const int pref[] = { 5, 5 };
	const int min[] = { 2, 2 };
	const int max[] = { 0, 0 };
	const int grow[] = { 1, 1 };
	const int shrink[] = { 1, 1 };
	const int want[] = { 4, 4 };
	int out[] = { 0, 0 };

	distribute_flex_axis(2, pref, min, max, grow, shrink, 8, out);
	assert_out_eq(out, want, 2);
}

static void test_shrink_last_resort(void)
{
	const int pref[] = { 5, 5 };
	const int min[] = { 2, 2 };
	const int max[] = { 0, 0 };
	const int grow[] = { 1, 1 };
	const int shrink[] = { 0, 0 };
	const int want[] = { 4, 2 };
	int out[] = { 0, 0 };

	distribute_flex_axis(2, pref, min, max, grow, shrink, 6, out);
	assert_out_eq(out, want, 2);
}

static void test_no_grow_keeps_pref(void)
{
	const int pref[] = { 3, 2, 1 };
	const int min[] = { 1, 1, 1 };
	const int max[] = { 0, 0, 0 };
	const int grow[] = { 0, 0, 0 };
	const int shrink[] = { 1, 1, 1 };
	const int want[] = { 3, 2, 1 };
	int out[] = { 0, 0, 0 };

	distribute_flex_axis(3, pref, min, max, grow, shrink, 12, out);
	assert_out_eq(out, want, 3);
}

int main(void)
{
	test_grow_with_remainder();
	test_grow_respects_max();
	test_shrink_proportional();
	test_shrink_last_resort();
	test_no_grow_keeps_pref();
	return 0;
}
