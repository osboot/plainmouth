#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

testcase_dump()
{
	"$topdir"/plainmouth plugin=meter action=create id=w1 \
		total=100 width=70 height=3 border=true
	"$topdir"/plainmouth action=set-value id=w1 value=80
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w1 value=100
	"$topdir"/plainmouth action=wait-result id=w1 > "$current_result"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_dump" "$current_result"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-e2e-meter" "$current_dump"
test ! -s "$current_result"
clear_testdata "$current_dump" "$current_result"
