#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

run_case()
{
	local buttons="$1" button="$2" expected_status="$3" expected_output="$4"
	shift 4
	"$topdir"/plaindialog --stdout "$@" >"$current_result" &
	local pid=$! id="plaindialog-$!" status=0 ready=false
	if [ "$MODE" = dump ]; then
		for ((n=0; n<100; n++)); do
			if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then
				ready=true
				break
			fi
			sleep 0.01
		done
		test "$ready" = true
		rm -f -- "$current_dump"
		"$topdir"/plainmouth action=dump id="$id" filename="$current_dump"
		grep -Fq -- "$buttons" "$current_dump"
		"$topdir"/plainmouth action=set-value id="$id" button="$button"
	fi
	wait "$pid" || status=$?
	if [ "$MODE" = dump ]; then
		test "$status" -eq "$expected_status"
		printf '%s' "$expected_output" | cmp - "$current_result"
	fi
	if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then
		return 1
	fi
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	run_case '[Accept now]' 1 0 '' --ok-label 'Accept now' --msgbox Message 6 40
	run_case '[Save][Back]' 1 0 initial --ok-label Save --cancel-label Back \
		--inputbox Name 7 40 initial
	run_case '[Save][Back]' 2 1 '' --ok-label Save --cancel-label Back \
		--inputbox Name 7 40 initial
	run_case '[Proceed][Stop]' 1 0 '' --ok-label Wrong --cancel-label Wrong \
		--yes-label Proceed --no-label Stop --yesno Continue 6 40
	run_case '[Proceed][Stop]' 2 1 '' --yes-label Proceed --no-label Stop \
		--yesno Continue 6 40
	run_case '[EXIT]' 1 0 '' --ok-label Wrong --textbox "$testsdir/expected-e2e-msgbox" 10 48
	run_case '[Dismiss]' 1 0 '' --exit-label Dismiss --textbox "$testsdir/expected-e2e-msgbox" 10 48
	run_case '[Close file]' 1 0 '' --exit-label 'Close file' --ok-label Wrong \
		--tailbox "$testsdir/expected-e2e-msgbox" 10 48
	run_case '[Done]' 1 0 '' --ok-label Done --termbox 'exit 0' 10 48
	run_case '[]' 1 0 '' --ok-label '' --msgbox Message 6 40
	run_case '[Last]' 1 0 '' --ok-label First --ok-label Last --exit-label Wrong \
		--msgbox Message 6 40
	run_case '[Pick][Back]' 1 0 tag --ok-label Pick --cancel-label Back \
		--menu Choose 9 40 2 tag Item
	if [ "$MODE" = dump ]; then
		local option status
		for option in --ok-label --cancel-label --yes-label --no-label --exit-label; do
			status=0
			"$topdir"/plaindialog "$option" >/dev/null 2>&1 || status=$?
			test "$status" -eq 255
		done
	fi
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_result"
