#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=password action=create id=w1 x=1 y=1 width=40 height=5 border=true \
		text="(1) Enter password:
123456789 123456789 123456789" \
		label="qwerty asdfghjk: "

	"$topdir"/plainmouth \
		plugin=password action=create id=w2 x=20 y=4 width=30 height=4 border=true \
		text="(2) Enter password:"

	"$topdir"/plainmouth \
		plugin=password action=create id=w3 width=20 height=3 x=36 y=6 border=true
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth action=wait-result id=w2
	"$topdir"/plainmouth action=wait-result id=w3
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth action=dump id=w2 filename="$current_dump"
	"$topdir"/plainmouth action=dump id=w3 filename="$current_dump"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_${MODE:-dump}" &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump"
