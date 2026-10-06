#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

draw_checklist()
{
	"$topdir"/plainmouth plugin=checklistbox action=create id=w1 \
		width=24 height=6 border=true select=2 visible=3 \
		option=apple status=true option=banana status=yes option=orange status=false button=OK
}

draw_radiolist()
{
	"$topdir"/plainmouth plugin=checklistbox action=create id=w2 \
		width=24 height=6 border=true select=1 visible=3 \
		option=first status=false option=second status=true option=third button=OK
}

expect_create_error()
{
	local expected="$1" actual status=0
	shift
	actual=$("$topdir"/plainmouth plugin=checklistbox action=create id=bad width=24 height=6 "$@") || status=$?
	test "$status" -eq 1
	case "$actual" in
		*"ERR=$expected"*) ;;
		*) return 1 ;;
	esac
}

testcase_view()
{
	draw_checklist
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth action=delete id=w1
	draw_radiolist
	"$topdir"/plainmouth action=wait-result id=w2
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_checklist
	"$topdir"/plainmouth action=result id=w1 > "$current_result"
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	local before after
	before=$("$topdir"/plainmouth action=result id=w1)
	if "$topdir"/plainmouth action=set-value id=w1 option=3 > /dev/null; then
		return 1
	fi
	after=$("$topdir"/plainmouth action=result id=w1)
	test "$before" = "$after"
	"$topdir"/plainmouth action=set-value id=w1 option=1 selected=false
	"$topdir"/plainmouth action=set-value id=w1 option=3 selected=true
	"$topdir"/plainmouth action=result id=w1 >> "$current_result"

	draw_radiolist
	"$topdir"/plainmouth action=result id=w2 >> "$current_result"
	"$topdir"/plainmouth action=dump id=w2 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w2 option=3 selected=true
	"$topdir"/plainmouth action=result id=w2 >> "$current_result"
	"$topdir"/plainmouth action=set-value id=w2 option=3 selected=false
	"$topdir"/plainmouth action=set-value id=w2 option=1 selected=true
	"$topdir"/plainmouth action=result id=w2 >> "$current_result"

	expect_create_error "invalid value: status" option=one status=maybe
	expect_create_error "status must follow option" status=true option=one
	expect_create_error "status must follow option" option=one button=OK status=true
	expect_create_error "status must follow option" option=one status=true status=false
	expect_create_error "initial selection exceeds select limit" select=1 option=one status=true option=two status=true
	expect_create_error "initial selection exceeds select limit" select=2 option=one status=true option=two status=true option=three status=true
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
rm -f -- "$current_dump" "$current_result"
run_test "testcase_${MODE:-dump}" &
run_server
if [ "$MODE" = dump ]; then
	diff -u "$testsdir/expected-e2e-checklist-initial-result" "$current_result"
fi
verify_dump "$current_dump"
clear_testdata "$current_dump" "$current_result"
