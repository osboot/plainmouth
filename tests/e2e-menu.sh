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

draw_testcase()
{
	"$topdir"/plainmouth plugin=menubox action=create id=w1 \
		width=24 height=6 border=true visible=3 button=OK \
		option=apple option=banana option=orange option=mango \
		option=pineapple option=grapes option=raspberry button=Cancel
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth action=delete id=w1
	draw_second_testcase
	"$topdir"/plainmouth action=wait-result id=w2
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=result id=w1 > "$current_result"
	"$topdir"/plainmouth action=set-value id=w1 option=2
	"$topdir"/plainmouth action=set-value id=w1 option=6
	expect_error "invalid value: option" option=bad
	expect_error "invalid value: option" option=0
	expect_error "option not found: option=99" option=99
	expect_error "invalid value: finished" option=2 finished=maybe
	expect_error "ambiguous target: button and option" button=1 option=2
	expect_error "invalid value: button" button=bad
	expect_error "invalid value: clicked" button=1 clicked=maybe
	expect_error "widget not found: button=99" button=99
	"$topdir"/plainmouth action=set-value id=w1 button=2 clicked=false
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w1 button=1
	"$topdir"/plainmouth action=wait-result id=w1 >> "$current_result"
	draw_second_testcase
	"$topdir"/plainmouth action=set-value id=w2 option=2 finished=true
	"$topdir"/plainmouth action=wait-result id=w2 >> "$current_result"
	"$topdir"/plainmouth --quit
}

draw_second_testcase()
{
	"$topdir"/plainmouth plugin=menubox action=create id=w2 \
		width=24 height=4 border=true visible=2 option=first option=second
}

exec 2>"$logfile"
rm -f -- "$current_dump" "$current_result"
run_test "testcase_${MODE:-dump}" &
run_server
if [ "$MODE" = dump ]; then
	diff -u "$testsdir/expected-e2e-menu-result" "$current_result"
fi
verify_dump "$current_dump"
clear_testdata "$current_dump" "$current_result"
