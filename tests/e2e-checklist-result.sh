#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

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

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=set-value id=w1 select=1 option=6 selected=true
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w1 button=1 clicked=true
	"$topdir"/plainmouth action=wait-result id=w1 > "$current_result"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_dump" "$current_result"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-${progname%.sh}-dump" "$current_dump"
diff -u "$testsdir/expected-${progname%.sh}" "$current_result"
clear_testdata "$current_dump" "$current_result"
