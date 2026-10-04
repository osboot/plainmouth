#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

testcase()
{
	local waiter status=0
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
	sleep 0.1
	kill -0 "$waiter"
	"$topdir"/plainmouth --quit
	wait "$waiter" || status=$?
	test "$status" != 0
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server
wait "$test_pid"
clear_testdata "$current_dump"
