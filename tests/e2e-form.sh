#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=form action=create id=w1 width=30 height=5 border=true \
		hbox=start label="Username:" input="legion" hbox=end \
		hbox=start label="Password:" password="" hbox=end \
		\
		button="OK" button="Cancel" \
	#
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
