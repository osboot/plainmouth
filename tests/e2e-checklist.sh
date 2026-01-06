#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=checklist action=create id=w1 width=40 height=7 border=true \
		select=2 visible=5 \
		option="apple" \
		option="banana" \
		option="orange" \
		option="mango" \
		option="pineapple" \
		option="grapes" \
		option="raspberry" \
		button="OK" \
		button="Cancel" \
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
