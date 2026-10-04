#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

create_range()
{
	"$topdir"/plainmouth action=create plugin=rangebox id=range width=32 height=7 \
		border=true text=Range button=OK button=Cancel "$@"
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	if [ "$MODE" = view ]; then
		"$topdir"/plaindialog --rangebox Range 7 40 -100 100 25
		return
	fi
	create_range min=-100 max=100 value=25
	"$topdir"/plainmouth action=set-value id=range value=-42
	"$topdir"/plainmouth action=dump id=range filename="$current_dump"
	grep -Fq -- '-042' "$current_dump"
	test "$("$topdir"/plainmouth action=result id=range)" = "$(printf 'VALUE=-42\nBUTTON_1=0\nBUTTON_2=0')"
	if "$topdir"/plainmouth action=set-value id=range value=bad; then return 1; fi
	if "$topdir"/plainmouth action=set-value id=range button=1 value=10; then return 1; fi
	"$topdir"/plainmouth action=set-value id=range value=1000
	test "$("$topdir"/plainmouth action=result id=range)" = "$(printf 'VALUE=100\nBUTTON_1=0\nBUTTON_2=0')"
	"$topdir"/plainmouth action=set-value id=range value=-1000
	"$topdir"/plainmouth action=set-value id=range button=1
	test "$("$topdir"/plainmouth action=wait-result id=range)" = "$(printf 'VALUE=-100\nBUTTON_1=1\nBUTTON_2=0')"
	"$topdir"/plainmouth action=delete id=range
	if create_range min=2 max=1 value=1; then return 1; fi
	if create_range min=0 max=1 value=2; then return 1; fi
	if create_range min=0 max=bad value=0; then return 1; fi
	create_range min=-2147483648 max=2147483647 value=2147483647
	"$topdir"/plainmouth action=set-value id=range value=-2147483648
	"$topdir"/plainmouth action=delete id=range
	create_range min=5 max=5 value=5
	"$topdir"/plainmouth action=delete id=range
}

exec 2>"$logfile"
run_test testcase &
run_server
clear_testdata "$current_dump"
