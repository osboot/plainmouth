#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_error()
{
	local status=0
	"$topdir"/plainmouth "$@" >/dev/null || status=$?
	test "$status" -eq 1
}

define_theme()
{
	"$topdir"/plainmouth action=set-style style=warning name=window fg=yellow bg=red attrs=bold
	"$topdir"/plainmouth action=set-style style=warning name=focus fg=black bg=yellow attrs=underline
}

create_dialogs()
{
	"$topdir"/plainmouth action=create plugin=msgbox id=left style=warning \
		x=2 y=2 width=36 height=6 border=true text='Shared theme: left' button=OK
	"$topdir"/plainmouth action=create plugin=msgbox id=right style=warning \
		x=40 y=2 width=36 height=6 border=true text='Shared theme: right' button=OK
}

view_step()
{
	local result
	"$topdir"/plainmouth action=create plugin=msgbox id=step \
		x=2 y=12 width=74 height=6 border=true text="$1" button=Next button=Close
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
	define_theme
	create_dialogs
	view_step 'Both windows appear with the warning theme.' || return 0
	"$topdir"/plainmouth action=set-style id=left name=window bg=blue attrs=normal
	view_step 'Local override: left has a blue background and normal text.' || return 0
	"$topdir"/plainmouth action=set-style style=warning name=window fg=white bg=magenta attrs=underline
	view_step 'Theme update: left inherits foreground; right inherits all.' || return 0
	"$topdir"/plainmouth action=set-style id=left name=window reset=true
	view_step 'Local reset: both windows use the updated theme.' || return 0
	"$topdir"/plainmouth action=set-style style=warning name=window reset=true
	view_step 'Theme reset: both windows inherit the global window style.' || return 0
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	expect_error action=set-style style=broken name=window attrs=unknown
	expect_error action=create plugin=msgbox id=bad style=broken width=20 height=6 button=OK
	expect_error action=create plugin=msgbox id=bad style=missing width=20 height=6 button=OK
	expect_error action=set-style style= name=window attrs=bold
	expect_error action=set-style style=missing name=window reset=true
	define_theme
	create_dialogs
	expect_error action=set-style id=left style=warning name=window attrs=bold
	expect_error action=set-style style=warning name=main attrs=bold
	expect_error action=set-style style=warning name=window attrs=unknown bg=blue
	"$topdir"/plainmouth action=set-style id=left name=window bg=blue attrs=normal
	"$topdir"/plainmouth action=set-style style=warning name=window fg=white attrs=underline
	"$topdir"/plainmouth action=set-style name=window fg=white bg=cyan attrs=dim
	"$topdir"/plainmouth action=set-style id=left name=window reset=true
	"$topdir"/plainmouth action=set-style style=warning name=window reset=true
	"$topdir"/plainmouth action=set-value id=left button=1
	"$topdir"/plainmouth action=wait-result id=left
	"$topdir"/plainmouth action=delete id=left
	"$topdir"/plainmouth action=create plugin=msgbox id=left style=warning \
		width=20 height=6 button=OK
	"$topdir"/plainmouth action=set-style style=warning name=window attrs=bold
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_$MODE" &
run_server
clear_testdata "$current_dump"
