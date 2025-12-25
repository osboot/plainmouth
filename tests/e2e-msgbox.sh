#!/bin/sh -efu

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_dump="/tmp/plainmouth-$$.dump"
logfile="$testsdir/$progname.log"

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=msgbox action=create id=w1 width=40 height=7 border=true \
		text="ВАЖНО!
тут какое-то важное сообщение." \
		button="OK" \
		button="Cancel" \
		button="второе" \
		button="ещё что-то"
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
