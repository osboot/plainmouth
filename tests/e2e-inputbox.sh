#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

expect_error()
{
	local expected="$1" actual status=0
	shift
	actual=$("$topdir"/plainmouth action=set-value id=w1 "$@") || status=$?
	test "$status" -eq 1
	test "$actual" = "ERR=$expected"
}

testcase_dump()
{
	"$topdir"/plainmouth plugin=inputbox action=create id=w1 \
		width=32 height=5 border=true text="Enter a name:" \
		label="Name: " value=initial placeholder="Your name" \
		button=OK button=Cancel
	"$topdir"/plainmouth action=result id=w1 > "$current_result"
	expect_error "field is missing: value, finished or button"
	expect_error "invalid value: finished" value=changed finished=maybe
	expect_error "invalid value: button" button=bad
	expect_error "invalid value: clicked" button=1 clicked=maybe
	expect_error "widget not found: button=99" button=99
	expect_error "ambiguous target: button and input" button=1 value=changed
	"$topdir"/plainmouth action=result id=w1 >> "$current_result"
	"$topdir"/plainmouth action=set-value id=w1 value=alice
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w1 button=2
	"$topdir"/plainmouth action=wait-result id=w1 >> "$current_result"

	"$topdir"/plainmouth plugin=inputbox action=create id=w2 \
		width=24 height=3 border=true placeholder="Type here"
	"$topdir"/plainmouth action=dump id=w2 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w2 value= finished=true
	"$topdir"/plainmouth action=wait-result id=w2 >> "$current_result"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_dump" "$current_result"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-e2e-inputbox-result" "$current_result"
verify_dump "$current_dump"
clear_testdata "$current_dump" "$current_result"
