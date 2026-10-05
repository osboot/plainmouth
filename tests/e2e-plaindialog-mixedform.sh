#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

export LC_ALL=C.UTF-8
current_result="$testsdir/$progname.result"

run_case()
{
	local kind="$1" button="$2" expected_status="$3" expected_output="$4"
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
		"$topdir"/plainmouth action=set-value id="$id" input=1 value=changed
		local empty=3
		if [ "$kind" = mixed ]; then
			empty=7
			"$topdir"/plainmouth action=set-value id="$id" input=2 value=newsecret
			for n in 3 4 5 6; do
				if "$topdir"/plainmouth action=set-value id="$id" input="$n" value=changed; then
					return 1
				fi
			done
		else
			if "$topdir"/plainmouth action=set-value id="$id" input=2 value=changed; then
				return 1
			fi
		fi
		"$topdir"/plainmouth action=set-value id="$id" input="$empty" value=erase
		"$topdir"/plainmouth action=set-value id="$id" input="$empty" value=
		"$topdir"/plainmouth action=dump id="$id" filename="$current_dump"
		if [ "$kind" = mixed ]; then
			grep -q changed "$current_dump"
			grep -q fixed "$current_dump"
			grep -Fq '*********' "$current_dump"
			! grep -Eq 'newsecret|hidden|masked' "$current_dump"
		else
			grep -Fq '*******' "$current_dump"
			grep -Fq '********' "$current_dump"
			! grep -Eq 'changed|readonly' "$current_dump"
		fi
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
	local mixed=(Plain 1 1 plain 1 10 12 12 0 Secret 2 1 secret 2 10 12 12 1
		Fixed 3 1 fixed 3 10 12 3 2 Locked 4 1 hidden 4 10 12 3 3
		Omit 5 1 omit 5 10 -12 0 0 Private 6 1 masked 6 10 -12 0 1
		Empty 7 1 '' 7 10 12 12 1)
	local password=(Secret 1 1 secret 1 10 12 12 Fixed 2 1 readonly 2 10 -12 0
		Empty 3 1 '' 3 10 12 12)
	run_case mixed 1 0 $'changed\nnewsecret\nfixed\nhidden\n\n' \
		--mixedform Mixed 14 48 8 "${mixed[@]}"
	run_case password 1 0 $'changed\n\n' --passwordform Password 10 48 4 "${password[@]}"
	[ "$MODE" = dump ] || return 0
	run_case mixed 1 0 'changed|newsecret|fixed|hidden||' --output-separator '|' \
		--mixedform Mixed 14 48 8 "${mixed[@]}"
	run_case mixed 2 1 '' --mixedform Mixed 14 48 8 "${mixed[@]}"
	run_case password 2 1 '' --passwordform Password 10 48 4 "${password[@]}"
	for flag in -1 4 invalid 2147483648; do
		invalid_case --mixedform Mixed 10 48 4 Name 1 1 value 1 10 12 12 "$flag"
	done
	invalid_case --mixedform Mixed 10 48 4 Name 1 1 value 1 10 12 12
	invalid_case --passwordform Password 10 48 4 Name 1 1 value 1 10 12
	invalid_case --separate-output --mixedform Mixed 14 48 8 "${mixed[@]}"
	invalid_case --separate-output --passwordform Password 10 48 4 "${password[@]}"
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_result"
