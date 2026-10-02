#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=timebox action=create id=w1 width=16 height=4 border=true \
		button="OK" \
		button="Cancel" \
	#
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=set-value id=w1 spinbox=1 value=23
	"$topdir"/plainmouth action=set-value id=w1 spinbox=2 value=59
	"$topdir"/plainmouth action=set-value id=w1 spinbox=3 value=58
	"$topdir"/plainmouth action=set-value id=w1 button=1 clicked=true
	"$topdir"/plainmouth action=wait-result id=w1 > "$current_result"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_result"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-${progname%.sh}" "$current_result"
clear_testdata "$current_result"
