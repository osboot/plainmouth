#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

taildir=$(mktemp -d "$testsdir/tailbox.XXXXXX")
tailfile="$taildir/file"
probe="$taildir/probe"

await_text()
{
	local id="$1" text="$2"
	for ((n=0; n<60; n++)); do
		rm -f -- "$probe"
		"$topdir"/plainmouth action=dump id="$id" filename="$probe"
		if grep -Fq -- "$text" "$probe"; then
			return 0
		fi
		sleep 0.01
	done
	cat "$probe"
	return 1
}

create_tail()
{
	"$topdir"/plainmouth action=create plugin=tailbox id="$1" \
		file="$2" width=24 height=4
}

testcase_dump()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	printf 'zero\none\ntwo\nthree\nfour\nfive\n' >"$tailfile"
	create_tail w1 "$tailfile"
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	printf 'six' >>"$tailfile"
	await_text w1 six
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	# A multibyte character may be split between timer notifications.
	printf '\303' >>"$tailfile"
	sleep 0.15
	await_text w1 six
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	printf '\251\n' >>"$tailfile"
	await_text w1 $'six\303\251'
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	printf 'new\n' >"$tailfile"
	await_text w1 new
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	printf 'independent\n' >"$taildir/second"
	create_tail w2 "$taildir/second"
	printf 'next\n' >>"$taildir/second"
	await_text w2 next
	"$topdir"/plainmouth action=delete id=w1
	printf 'still alive\n' >>"$taildir/second"
	await_text w2 'still alive'
	"$topdir"/plainmouth action=dump id=w2 filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=w2 button=1
	test "$("$topdir"/plainmouth action=wait-result id=w2)" = BUTTON_1=1
	"$topdir"/plainmouth action=delete id=w2
	# Start at the end of a file exceeding the retained buffer.
	printf '%s\n' {1..15000} >"$tailfile"
	printf 'latest\n' >>"$tailfile"
	create_tail large "$tailfile"
	await_text large latest
	"$topdir"/plainmouth action=delete id=large
	# Reuse IDs and descriptors while timers are active.
	for ((i=0; i<8; i++)); do
		create_tail reused "$tailfile"
		"$topdir"/plainmouth action=delete id=reused
	done
	local status=0
	create_tail bad "$taildir/missing" >/dev/null || status=$?
	test "$status" -eq 1
	status=0
	create_tail bad "$taildir" >/dev/null || status=$?
	test "$status" -eq 1
	mkfifo "$taildir/fifo"
	status=0
	create_tail bad "$taildir/fifo" >/dev/null || status=$?
	test "$status" -eq 1
	# The compatibility client waits for the server's tailbox and emits no text.
	"$topdir"/plaindialog --stdout --tailbox "$tailfile" 6 32 >"$taildir/result" &
	local pid=$! id="plaindialog-$!"
	for ((i=0; i<60; i++)); do
		if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then
			break
		fi
		sleep 0.01
	done
	"$topdir"/plainmouth action=set-value id="$id" button=1
	wait "$pid"
	test ! -s "$taildir/result"
	if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then
		return 1
	fi
}

testcase_view()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	printf 'Watching a growing file\n' >"$tailfile"
	"$topdir"/plaindialog --tailbox "$tailfile" 10 60 &
	local pid=$!
	for ((i=1; i<=30; i++)); do
		kill -0 "$pid" 2>/dev/null || break
		printf 'line %02d: a growing log with a long line for horizontal scrolling\n' "$i" >>"$tailfile"
		sleep 0.2
	done
	wait "$pid"
}

exec 2>"$logfile"
run_test "testcase_$MODE" &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump" "$probe" "$tailfile" "$taildir/second" \
	"$taildir/fifo" "$taildir/result"
rmdir "$taildir"
