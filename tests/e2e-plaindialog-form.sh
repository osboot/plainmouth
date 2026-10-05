#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

export LC_ALL=C.UTF-8
current_result="$testsdir/$progname.result"
client=("$topdir"/plaindialog)
if [ "${CHECK_CLIENT:-no}" = yes ]; then
	client=("$valgrind_prog" --leak-check=full --error-exitcode=99
		--log-file="$testsdir/$progname-client-%p.log" "${client[@]}")
fi

run_case()
{
	local mode="$1" button="$2" expected_status="$3" expected_output="$4"
	shift 4
	"${client[@]}" --stdout "$@" >"$current_result" &
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
		if [ "$mode" = edit ]; then
			"$topdir"/plainmouth action=set-value id="$id" input=2 value=TOP
			"$topdir"/plainmouth action=dump id="$id" filename="$current_dump"
			grep -q TOP "$current_dump"
			grep -q fixed "$current_dump"
			if "$topdir"/plainmouth action=set-value id="$id" input=2 value=1234567; then
				return 1
			fi
			if "$topdir"/plainmouth action=set-value id="$id" input=3 value=changed; then
				return 1
			fi
			if "$topdir"/plainmouth action=set-value id="$id" input=4 value=changed; then
				return 1
			fi
			"$topdir"/plainmouth action=set-value id="$id" input=5 value=erase
			"$topdir"/plainmouth action=set-value id="$id" input=5 value=
			"$topdir"/plainmouth action=set-value id="$id" input=1 value=BOTTOM
			"$topdir"/plainmouth action=dump id="$id" filename="$current_dump.bottom"
			grep -q BOTTOM "$current_dump.bottom"
			! grep -q TOP "$current_dump.bottom"
			! cmp -s "$current_dump" "$current_dump.bottom"
		fi
		if [ "$mode" = signal ]; then
			kill -TERM "$pid"
		else
			"$topdir"/plainmouth action=set-value id="$id" button="$button"
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

invalid_case()
{
	local status=0
	"$topdir"/plaindialog --stdout "$@" >"$current_result" 2>/dev/null || status=$?
	test "$status" -eq 255
	test ! -s "$current_result"
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	local fields=(Bottom 12 28 b 12 36 6 12 Top 1 1 a 1 8 6 0
		Fixed 2 1 locked 2 8 -8 0 Auto 3 1 fixed 3 8 0 0 Empty 4 1 '' 4 8 6 6)
	run_case edit 1 0 $'BOTTOM\nTOP\n\n' --ok-label Save --cancel-label Back \
		--form Form 10 48 4 "${fields[@]}"
	[ "$MODE" = dump ] || return 0
	run_case edit 1 0 'BOTTOM|TOP||' --output-separator '|' --form Form 10 48 4 "${fields[@]}"
	run_case plain 1 0 'ba' --separator '' --form Form 10 48 4 "${fields[@]}"
	run_case edit 2 1 '' --form Form 10 48 4 "${fields[@]}"
	run_case plain 1 0 $'initial\n' --form '' 10 48 0 '' 0 -1 initial 0 0 8 0
	run_case plain 1 0 '' --form Readonly 10 48 3 '' 1 1 '' 1 8 0 0
	run_case plain 1 0 $'\xc3\xa9\n' --form Unicode 10 48 3 $'\xe7\x95\x8c' 1 1 $'\xc3\xa9' 1 8 1 1
	run_case plain 1 0 'a b"c::' --output-separator ignored --separator '::' \
		--form Literal 10 48 3 Name 1 1 'a b"c' 1 8 12 12
	run_case signal 1 255 '' --form Interrupted 10 48 4 "${fields[@]}"
	invalid_case --form Form 10 48 -1 "${fields[@]}"
	invalid_case --form Form 10 48 4 Name 1 1 value 1 8 6
	invalid_case --form Form 10 48 4 Name invalid 1 value 1 8 6 6
	invalid_case --form Form 10 48 4 Name 1 1 value 1 8 -2147483648 6
	invalid_case --form Form 10 48 4 Name 1 1 value 1 8 6 -1
	invalid_case --form Form 10 48 4 Name 1 1 value 1 8 6 2147483647
	invalid_case --form Form 10 48 4 Name 1 1 value 1 4096 6 6
	invalid_case --form Form 10 48 4 Name 1 1 toolong 1 8 6 0
	invalid_case --form Form 10 48 4 Name 1 1 $'bad\nvalue' 1 8 20 20
	invalid_case --separate-output --form Form 10 48 4 "${fields[@]}"
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.bottom" "$current_result"
