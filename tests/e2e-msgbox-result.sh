#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

testcase_dump()
{
	"$topdir"/plainmouth plugin=msgbox action=create id=w1 \
		width=40 height=7 border=true text="Confirm action" \
		button="OK" button="Cancel"
	"$topdir"/plainmouth action=set-value id=w1 button=1 clicked=true
	"$topdir"/plainmouth action=set-value id=w1 button=1 clicked=false
	"$topdir"/plainmouth action=set-value id=w1 button=2 clicked=true
	"$topdir"/plainmouth action=wait-result id=w1 > "$current_result"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_result"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-${progname%.sh}" "$current_result"
clear_testdata "$current_result"
