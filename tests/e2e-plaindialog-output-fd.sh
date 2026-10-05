#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

run_case()
{
	local expected_status="$1" expected="$2" button="$3"
	shift 3
	"$topdir"/plaindialog "$@" >"$current_result.stdout" \
		3>"$current_result" 4>/dev/full &
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
		"$topdir"/plainmouth action=set-value id="$id" value=changed
		"$topdir"/plainmouth action=set-value id="$id" button="$button"
	fi
	wait "$pid" || status=$?
	if [ "$MODE" = dump ]; then
		test "$status" -eq "$expected_status"
		printf '%s' "$expected" | cmp - "$current_result"
		if [[ " $* " = *' --stdout --inputbox '* ]]; then
			test "$(<"$current_result.stdout")" = changed
		else
			test ! -s "$current_result.stdout"
		fi
	fi
	! "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	run_case 0 changed 1 --output-fd 3 --inputbox Name 7 40 initial
	[ "$MODE" = dump ] || return 0
	run_case 0 changed 1 --stdout --output-fd 3 --inputbox Name 7 40 initial
	run_case 0 '' 1 --output-fd 3 --stdout --inputbox Name 7 40 initial
	run_case 1 '' 2 --output-fd 3 --inputbox Name 7 40 initial
	run_case 255 '' 1 --output-fd 4 --inputbox Name 7 40 initial
	local fd status
	for fd in -1 invalid 2147483648 9; do
		status=0
		"$topdir"/plaindialog --output-fd "$fd" --msgbox Message 6 40 \
			9>&- >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
	done
	status=0
	"$topdir"/plaindialog --output-fd >/dev/null 2>&1 || status=$?
	test "$status" -eq 255
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_result" "$current_result.stdout"
