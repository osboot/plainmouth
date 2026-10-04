#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

create_instance()
{
	"$topdir"/plainmouth action=create plugin=msgbox "id=$1" \
		width=40 height=6 border=true text=Lifecycle button=OK
}

testcase()
{
	local first second status
	trap '"$topdir"/plainmouth action=delete id=first >/dev/null 2>&1 || :; "$topdir"/plainmouth action=delete id=second >/dev/null 2>&1 || :; "$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	create_instance first
	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=first
		return
	fi
	create_instance second
	"$topdir"/plainmouth action=focus id=first
	"$topdir"/plainmouth action=wait-result id=first >"$current_dump.first" &
	first=$!
	"$topdir"/plainmouth action=wait-result id=first >"$current_dump.second" &
	second=$!
	sleep 0.1
	kill -0 "$first"
	kill -0 "$second"
	"$topdir"/plainmouth action=set-value id=first button=1
	wait "$first"
	wait "$second"
	test "$(<"$current_dump.first")" = BUTTON_1=1
	test "$(<"$current_dump.second")" = BUTTON_1=1
	"$topdir"/plainmouth action=delete id=first
	"$topdir"/plainmouth action=focus id=second
	"$topdir"/plainmouth action=wait-result id=second >"$current_dump.first" &
	first=$!
	sleep 0.1
	kill -0 "$first"
	"$topdir"/plainmouth action=delete id=second
	status=0
	wait "$first" || status=$?
	test "$status" = 1
	test "$(<"$current_dump.first")" = 'ERR=no instance'
	create_instance first
	"$topdir"/plainmouth action=focus id=first
	"$topdir"/plainmouth action=set-value id=first button=1
	test "$("$topdir"/plainmouth action=wait-result id=first)" = BUTTON_1=1
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server
wait "$test_pid"
clear_testdata "$current_dump.first" "$current_dump.second"
