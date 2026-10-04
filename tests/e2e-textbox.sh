#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

work=$(mktemp -d "$testsdir/textbox.XXXXXX")
file="$work/file"

create_box()
{
	"$topdir"/plainmouth action=create plugin=textbox id=box file="$1" width=16 height=5 "${@:2}"
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	printf 'first\nsecond\nthird\nfourth\nfifth\n0123456789abcdefghijklmnop\nlast' >"$file"
	if [ "$MODE" = view ]; then
		"$topdir"/plaindialog --textbox "$file" 8 24
		return
	fi
	"$topdir"/plainmouth action=set-style style=reader name=window attrs=bold
	create_box "$file" style=reader
	"$topdir"/plainmouth action=dump id=box filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=box scroll-y=4 scroll-x=10
	"$topdir"/plainmouth action=dump id=box filename="$current_dump"
	printf 'changed' >"$file"
	"$topdir"/plainmouth action=dump id=box filename="$work/snapshot"
	grep -q abcdef "$work/snapshot"
	! grep -q changed "$work/snapshot"
	if "$topdir"/plainmouth action=set-value id=box scroll-y=0 scroll-x=-1; then return 1; fi
	"$topdir"/plainmouth action=dump id=box filename="$work/unchanged"
	cmp "$work/snapshot" "$work/unchanged"
	"$topdir"/plainmouth action=set-value id=box scroll-x=0 scroll-y=0
	"$topdir"/plainmouth action=dump id=box filename="$work/top"
	grep -q first "$work/top"
	"$topdir"/plainmouth action=set-value id=box button=1
	test "$("$topdir"/plainmouth action=wait-result id=box)" = BUTTON_1=1
	"$topdir"/plainmouth action=delete id=box
	for path in "$work/missing" "$work"; do
		if create_box "$path"; then return 1; fi
	done
	mkfifo "$work/fifo"
	if create_box "$work/fifo"; then return 1; fi
	truncate -s 65537 "$file"
	if create_box "$file"; then return 1; fi
	# Small input can still require an excessive rectangular pad.
	printf '%01000d\n' 0 >"$file"
	printf '\n%.0s' {1..2000} >>"$file"
	if create_box "$file"; then return 1; fi
	printf '\303\251\tvalue\000\377\303' >"$file"
	create_box "$file"
	"$topdir"/plainmouth action=delete id=box
	: >"$file"
	create_box "$file"
	"$topdir"/plainmouth action=delete id=box
	"$topdir"/plaindialog --stdout --textbox "$file" 6 24 >"$work/result" &
	local pid=$! id="plaindialog-$!" ready=false
	for ((n=0; n<100; n++)); do
		if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then ready=true; break; fi
		sleep 0.01
	done
	test "$ready" = true
	"$topdir"/plainmouth action=set-value id="$id" button=1
	wait "$pid"
	test ! -s "$work/result"
	if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then return 1; fi
}

exec 2>"$logfile"
run_test testcase &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump" "$file" "$work/fifo" "$work/result" \
	"$work/snapshot" "$work/top" "$work/unchanged"
rmdir "$work"
