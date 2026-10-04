#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

expect_error()
{
	local expected="$1" actual status=0
	shift
	actual=$("$topdir"/plainmouth action=set-value "$@") || status=$?
	test "$status" -eq 1
	test "$actual" = "ERR=$expected"
}

testcase_dump()
{
	"$topdir"/plainmouth plugin=msgbox action=create id=msg width=30 height=5 button=OK
	"$topdir"/plainmouth plugin=form action=create id=form width=40 height=7 \
		hbox=start label=Name input=initial hbox=end button=OK
	"$topdir"/plainmouth plugin=timebox action=create id=time width=16 height=4 button=OK
	"$topdir"/plainmouth plugin=checklist action=create id=list width=30 height=5 select=2 visible=2 option=one option=two button=OK
	"$topdir"/plainmouth plugin=password action=create id=pass width=30 height=5
	"$topdir"/plainmouth plugin=meter action=create id=meter total=100 width=70 height=3 border=true

	expect_error "field is missing: button" id=msg
	expect_error "field is missing: input or button" id=form
	expect_error "field is missing: spinbox or button" id=time
	expect_error "field is missing: option or button" id=list
	expect_error "field is missing: value, finished or button" id=pass
	expect_error "field is missing: value" id=meter
	expect_error "field is missing: value" id=form input=1
	expect_error "field is missing: value" id=time spinbox=1

	expect_error "invalid value: button" id=msg button=1junk
	expect_error "invalid value: clicked" id=msg button=1 clicked=maybe
	expect_error "widget not found: button=99" id=msg button=99
	expect_error "widget not found: input=99" id=form input=99 value=text
	expect_error "widget not found: spinbox=99" id=time spinbox=99 value=10
	expect_error "widget not found: select=99" id=list select=99 option=1
	expect_error "invalid value: option" id=list option=0
	expect_error "option not found: option=99" id=list option=99
	expect_error "invalid value: selected" id=list option=1 selected=maybe
	expect_error "invalid value: select" id=list select=bad option=1
	expect_error "invalid value: spinbox" id=time spinbox=bad value=10
	expect_error "invalid value: value" id=time spinbox=1 value=10junk
	expect_error "invalid value: value" id=meter value=
	expect_error "invalid value: value" id=meter value=999999999999999999999999
	expect_error "invalid value: input" id=form input=bad value=text
	expect_error "ambiguous target: button and input" id=form button=1 input=1 value=text
	expect_error "ambiguous target: button and spinbox" id=time button=1 spinbox=1 value=10
	expect_error "ambiguous target: button and option" id=list button=1 option=1
	expect_error "widget not found: button=99" id=pass button=99
	expect_error "ambiguous target: button and input" id=pass button=1 value=secret

	"$topdir"/plainmouth action=set-value id=form input=1 value=
	test "$("$topdir"/plainmouth action=result id=form)" = "$(printf 'INPUT_1=\nBUTTON_1=0')"

	"$topdir"/plainmouth action=set-value id=pass value=secret
	expect_error "invalid value: finished" id=pass value=changed finished=maybe
	"$topdir"/plainmouth action=set-value id=pass finished=yes
	test "$("$topdir"/plainmouth action=wait-result id=pass)" = "PASSWORD_1=secret"

	"$topdir"/plainmouth action=set-value id=meter value=80
	expect_error "field is missing: value" id=meter
	expect_error "invalid value: value" id=meter value=bad
	"$topdir"/plainmouth action=dump id=meter filename="$current_dump"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase_dump &
run_server
diff -u "$testsdir/expected-e2e-meter" "$current_dump"
clear_testdata "$current_dump"
