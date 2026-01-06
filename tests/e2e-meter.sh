#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=meter action=create id=w1 total=100 width=70 height=3 border=true

	for i in 10 20 30 40 50 60 70 80; do
		"$topdir"/plainmouth action=update id=w1 value=$i

		[ "$MODE" = dump ] ||
			sleep 0.3
	done

	[ "$MODE" != dump ] ||
		return 0

	for i in 90 100 110 120; do
		"$topdir"/plainmouth action=update id=w1 value=$i
		sleep 0.3
	done
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_${MODE:-dump}" &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump"
