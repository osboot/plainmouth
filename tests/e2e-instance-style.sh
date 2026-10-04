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

create_dialogs()
{
	"$topdir"/plainmouth action=create plugin=msgbox id=left x=2 y=2 \
		width=36 height=6 border=true text='Local style' button=OK
	"$topdir"/plainmouth action=create plugin=msgbox id=right x=40 y=2 \
		width=36 height=6 border=true text='Global style' button=OK
}

view_step()
{
	local result
	"$topdir"/plainmouth action=create plugin=msgbox id=step x=2 y=12 \
		width=74 height=6 border=true text="$1" button=Next button=Close
	"$topdir"/plainmouth action=focus id=step
	result=$("$topdir"/plainmouth action=wait-result id=step)
	"$topdir"/plainmouth action=delete id=step
	if printf '%s\n' "$result" | grep -qx BUTTON_2=1; then
		return 1
	fi
}

testcase_view()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	create_dialogs
	"$topdir"/plainmouth action=set-style id=left name=window fg=yellow attrs=bold
	view_step 'Left: yellow and bold; background inherited.' || return 0
	"$topdir"/plainmouth action=set-style name=window fg=white bg=cyan attrs=underline
	view_step 'Global change: both backgrounds; only right attributes.' || return 0
	"$topdir"/plainmouth action=set-style id=left name=window reset=true
	view_step 'Reset: both windows now use the global style.' || return 0
	"$topdir"/plainmouth action=set-style id=left name=window bg=red attrs=normal
	view_step 'Left: red background, normal attributes; foreground inherited.' || return 0
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	create_dialogs
	"$topdir"/plainmouth action=set-style id=left name=window fg=yellow attrs=bold
	"$topdir"/plainmouth action=set-style id=right name=focus bg=red
	"$topdir"/plainmouth action=set-style name=window fg=white bg=cyan attrs=underline
	expect_error id=missing name=window attrs=bold
	expect_error id=left name=main attrs=bold
	expect_error id=left name=focus reset=maybe
	expect_error id=left name=window reset=true attrs=normal
	expect_error id=left name=window attrs=bold,invalid bg=red
	expect_error id=left name=window fg=colorbogus
	expect_error name=window reset=true
	"$topdir"/plainmouth action=set-style id=left name=window reset=true
	"$topdir"/plainmouth action=set-style id=left name=window reset=true
	"$topdir"/plainmouth action=delete id=left
	"$topdir"/plainmouth action=create plugin=msgbox id=left \
		width=32 height=6 text='Recreated' button=OK
	"$topdir"/plainmouth action=set-style id=left name=button fg=red
	"$topdir"/plainmouth action=set-value id=left button=1
	"$topdir"/plainmouth action=wait-result id=left
	"$topdir"/plainmouth action=delete id=left
	"$topdir"/plainmouth action=set-value id=right button=1
	"$topdir"/plainmouth action=wait-result id=right
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_$MODE" &
run_server
clear_testdata "$current_dump"
