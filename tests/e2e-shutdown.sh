#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

testcase()
{
	local waiter event_waiter status=0
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	"$topdir"/plainmouth action=create plugin=msgbox id=shutdown \
		width=40 height=6 border=true text=Shutdown button=OK
	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=shutdown
		"$topdir"/plainmouth --quit
		return
	fi
	"$topdir"/plainmouth action=wait-result id=shutdown >"$current_dump" 2>/dev/null &
	waiter=$!
	"$topdir"/plainmouth action=create plugin=compose id=events width=32 height=6 \
		node=vbox node=button text=Test close=false node=end node=end
	"$topdir"/plainmouth action=wait-event id=events >"$current_dump.event" 2>/dev/null &
	event_waiter=$!
	sleep 0.1
	kill -0 "$waiter"
	kill -0 "$event_waiter"
	"$topdir"/plainmouth --quit
	wait "$waiter" || status=$?
	test "$status" != 0
	status=0
	wait "$event_waiter" || status=$?
	test "$status" != 0
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server
wait "$test_pid"
clear_testdata "$current_dump" "$current_dump.event"
