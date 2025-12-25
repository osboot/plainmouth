#!/bin/sh -efu

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_dump="/tmp/plainmouth-$$.dump"
logfile="$testsdir/$progname.log"

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=password action=create id=w1 x=20 y=4 width=30 height=3 border=true \
		label="Enter password:" \
		tooltip="Passwords must be at least 10 characters in length
a minimum of 1 lower case letter [a-z]
a minimum of 1 upper case letter [A-Z]
a minimum of 1 numeric character [0-9]
a minimum of 1 special character: ~\`!@#$%^&*()-_+={}[]|\;:\"<>,./?"
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
