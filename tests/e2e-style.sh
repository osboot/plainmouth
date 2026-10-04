#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_error()
{
	local status=0
	"$topdir"/plainmouth action=set-style "$@" >/dev/null || status=$?
	test "$status" -eq 1
}

view_case()
{
	local text="$1" name="$2" attrs="$3" fg="$4" bg="$5" result
	"$topdir"/plainmouth action=create plugin=msgbox id=style \
		width=48 height=8 border=true text="$text" button=Next button=Close
	sleep 1
	"$topdir"/plainmouth action=set-style name="$name" attrs="$attrs" fg="$fg" bg="$bg"
	result=$("$topdir"/plainmouth action=wait-result id=style)
	"$topdir"/plainmouth action=delete id=style
	if printf '%s\n' "$result" | grep -qx BUTTON_2=1; then
		return 1
	fi
}

testcase_view()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	view_case 'Window: bold and underline' window bold,underline yellow blue || return 0
	view_case 'Focused button: bold' focus bold black cyan || return 0
	view_case 'Unfocused button: italic' button italic white red || return 0
	view_case 'Window: reverse' window reverse white blue || return 0
	view_case 'Reset window attributes to normal' window normal white blue || return 0
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	"$topdir"/plainmouth action=create plugin=msgbox id=style \
		width=32 height=6 border=true text='Style attributes' button=OK
	"$topdir"/plainmouth action=set-style name=window attrs=bold
	"$topdir"/plainmouth action=set-style name=focus attrs=bold,underline
	"$topdir"/plainmouth action=set-style name=button fg=white bg=red attrs=italic
	"$topdir"/plainmouth action=set-style name=button fg=black bg=white
	"$topdir"/plainmouth action=set-style name=main attrs=dim
	expect_error name=unknown attrs=bold
	expect_error name=focus attrs=
	expect_error name=focus attrs=bold,
	expect_error name=focus attrs=normal,bold
	expect_error name=focus attrs=unknown fg=red bg=black
	expect_error name=focus attrs=bold fg=white
	expect_error name=focus attrs=bold fg=colorbogus bg=black
	expect_error name=focus attrs=bold fg=color-1 bg=black
	expect_error name=focus attrs=bold fg=color999999999999999999999 bg=black
	expect_error name=focus
	"$topdir"/plainmouth --ping >/dev/null
	"$topdir"/plainmouth action=set-style name=focus attrs=normal
	"$topdir"/plainmouth action=set-value id=style button=1
	"$topdir"/plainmouth action=wait-result id=style
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_$MODE" &
run_server
clear_testdata "$current_dump"
