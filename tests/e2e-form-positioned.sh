#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later
progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

testcase()
{
	trap '"$topdir"/plainmouth --quit' EXIT
	"$topdir"/plainmouth plugin=form action=create id=w1 width=30 height=8 border=true layout=positioned \
		field=start input=bottom x=35 y=15 width=10 max-length=12 field=end \
		field=start label=Name: x=0 y=0 width=6 field=end \
		field=start input=top x=7 y=0 width=10 max-length=12 field=end \
		field=start input=locked readonly=true x=0 y=2 width=8 field=end \
		field=start password=secret x=7 y=3 width=10 field=end \
		field=start input=unavailable disabled=true x=0 y=4 width=12 field=end \
		button=OK button=Cancel
	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=w1
		return
	fi
	"$topdir"/plainmouth action=set-value id=w1 input=2 value=TOP
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	grep -q TOP "$current_dump"
	if "$topdir"/plainmouth action=set-value id=w1 input=2 value=1234567890123; then
		return 1
	fi
	if "$topdir"/plainmouth action=set-value id=w1 input=3 value=changed; then
		return 1
	fi
	"$topdir"/plainmouth action=set-value id=w1 input=1 value=BOTTOM
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump.bottom"
	grep -q BOTTOM "$current_dump.bottom"
	! grep -q TOP "$current_dump.bottom"
	! cmp -s "$current_dump" "$current_dump.bottom"
	"$topdir"/plainmouth action=set-value id=w1 input=2 value=TOP
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	grep -q 'Name:' "$current_dump"
	grep -q TOP "$current_dump"
	"$topdir"/plainmouth action=result id=w1 > "$current_dump.result"
	grep -qx INPUT_1=BOTTOM "$current_dump.result"
	grep -qx INPUT_2=TOP "$current_dump.result"
	! grep -q INPUT_3= "$current_dump.result"
	grep -qx INPUT_4=secret "$current_dump.result"
	grep -qx INPUT_5=unavailable "$current_dump.result"
	"$topdir"/plainmouth action=set-value id=w1 button=1
	"$topdir"/plainmouth action=wait-result id=w1 > "$current_dump.result"
	grep -qx BUTTON_1=1 "$current_dump.result"
	"$topdir"/plainmouth action=delete id=w1
	if "$topdir"/plainmouth plugin=form action=create id=bad width=30 height=8 layout=positioned \
		field=start input=x x=-1 y=0 width=10 field=end button=OK; then
		return 1
	fi
	if "$topdir"/plainmouth plugin=form action=create id=bad width=30 height=8 layout=positioned \
		field=start input=x x=0 y=0 width=10 width=11 field=end button=OK; then
		return 1
	fi
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.bottom" "$current_dump.result"
