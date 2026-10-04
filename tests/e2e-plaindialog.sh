#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

current_result="$testsdir/$progname.result"
client=("$topdir"/plaindialog)
if [ "${CHECK_CLIENT:-no}" = yes ]; then
	client=("$valgrind_prog" --leak-check=full --error-exitcode=99
		--log-file="$testsdir/$progname-client-%p.log" "${client[@]}")
fi

run_case()
{
	local expected_status="$1" expected_output="$2" setter="$3"
	shift 3
	if [ "$setter" = default-stderr ]; then
		"${client[@]}" "$@" >/dev/null 2>"$current_result" &
	else
		"${client[@]}" "$@" >"$current_result" &
	fi
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
		if [ "$setter" = signal ]; then
			kill -TERM "$pid"
		else
			[ "$setter" != default-stderr ] || setter=finished=true
			if [[ "$setter" = option=* ]]; then
				local choice=false arg
				for arg in "$@"; do
					if [ "$arg" = --checklist ] || [ "$arg" = --radiolist ]; then
						choice=true
					fi
				done
				if [ "$choice" = true ]; then
					"$topdir"/plainmouth action=set-value id="$id" "$setter"
					"$topdir"/plainmouth action=set-value id="$id" button=1
				else
					"$topdir"/plainmouth action=set-value id="$id" "$setter" finished=true
				fi
			else
				"$topdir"/plainmouth action=set-value id="$id" "$setter"
			fi
		fi
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
	run_case 0 '' button=1 --msgbox 'Message' 6 32
	run_case 1 '' button=2 --yesno 'Continue?' 6 32
	run_case 0 initial button=1 --stdout --inputbox 'Name:' 7 32 initial
	run_case 0 secret button=1 --stdout --passwordbox 'Password:' 7 32 secret
	run_case 0 '07:08:09' button=1 --stdout --timebox 'Time:' 7 32 7 8 9
	run_case 0 'second tag' option=2 --stdout --menu 'Choose:' 9 40 3 \
		first 'First item' 'second tag' 'Second item'
	run_case 0 'second tag' option=2 --stdout --radiolist 'Choose one:' 10 44 3 \
		first 'First item' on 'second tag' 'Second item' off third 'Third item' off
	run_case 0 '"first tag" "third\"tag"' option=3 --stdout --checklist 'Choose several:' 10 48 3 \
		'first tag' 'First item' on second 'Second item' off 'third"tag' 'Third item' off
	if [ "$MODE" = dump ]; then
		run_case 0 initial default-stderr --inputbox 'Name:' 7 32 initial
		run_case 0 '' finished=true --stdout --inputbox 'Name:' 7 32
		run_case 1 '' button=2 --stdout --inputbox 'Name:' 7 32 secret
		run_case 1 '' button=2 --stdout --passwordbox 'Password:' 7 32 secret
		run_case 1 '' button=2 --stdout --timebox 'Time:' 7 32 12 34 56
		run_case 1 '' button=2 --stdout --menu 'Choose:' 9 40 3 first Item
		run_case 1 '' button=2 --stdout --radiolist 'Choose:' 9 40 3 first Item on
		run_case 0 '' button=1 --stdout --checklist 'Choose:' 9 40 3 first Item off
		run_case 255 '' signal --stdout --inputbox 'Interrupted:' 7 32
		local status=0
		"$topdir"/plaindialog --msgbox Text 0 20 >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --menu Text 8 30 2 tag >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --checklist Text 8 30 2 tag Item maybe >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --radiolist Text 8 30 2 tag Item >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --timebox Time 7 32 24 0 0 >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --timebox Time 7 32 0 minute 0 >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
		status=0
		"$topdir"/plaindialog --socket-file "$PLAINMOUTH_SOCKET.missing" \
			--msgbox Text 6 32 >/dev/null 2>&1 || status=$?
		test "$status" -eq 255
	fi
}

exec 2>"$logfile"
run_test testcase &
run_server
clear_testdata "$current_result"
