#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=formbox action=create id=w1 width=30 height=5 border=true \
		hbox=start label="Username:" input="legion" hbox=end \
		hbox=start label="Password:" password="" hbox=end \
		button="OK" button="Cancel" \
	#
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=set-value id=w1 input=1 value="alice"
	"$topdir"/plainmouth action=set-value id=w1 input=2 value="secret"
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
