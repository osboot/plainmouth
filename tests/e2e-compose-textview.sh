#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_error()
{
	local actual status=0
	actual=$("$topdir"/plainmouth "$@") || status=$?
	test "$status" -eq 1

	case "$actual" in
		ERR=*) ;;
		*) return 1 ;;
	esac
}

replace_text()
{
	"$topdir"/plainmouth action=set-value id=output node-id=output "value=$1"
}

testcase()
{
	local text=$'First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line\nLast line'
	"$topdir"/plainmouth action=create plugin=compose id=output width=32 height=8 border=true \
		node=vbox \
		  node=textview node-id=output "text=$text" flex-h=1 node=end \
		  node=hbox \
		    node=button node-id=replace text=Replace close=false node=end \
		    node=button node-id=ok text=OK node=end \
		  node=end \
		node=end

	if [ "$MODE" = view ]; then
		while "$topdir"/plainmouth action=wait-event id=output >/dev/null; do
			replace_text "$text"$'\nAdditional line\nAnother line\nEnd of replacement'
			text=$'Replacement\nShorter content'
		done

		"$topdir"/plainmouth action=wait-result id=output
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=output filename="$current_dump"
	grep -Fq 'First line' "$current_dump"
	! grep -Fq 'Last line' "$current_dump"
	replace_text $'New first\nNew second\nNew third\nNew fourth\nNew fifth\nNew sixth\nNew last'
	expect_error action=set-value id=output node-id=output text=bad
	expect_error action=set-value id=output node-id=output
	expect_error action=set-value id=output node-id=output value=one value=two
	local oversized rectangle
	printf -v oversized '%4097s' ''
	oversized=${oversized// /x}
	expect_error action=set-value id=output node-id=output "value=$oversized"
	printf -v rectangle '%2000s' ''
	rectangle=${rectangle// /x}
	printf -v oversized '%2000s' ''
	rectangle+=${oversized// /$'\n'}
	expect_error action=set-value id=output node-id=output "value=$rectangle"
	"$topdir"/plainmouth action=dump id=output filename="$current_dump"
	grep -Fq 'New first' "$current_dump"
	! grep -Fq 'First line' "$current_dump"
	replace_text Short
	"$topdir"/plainmouth action=dump id=output filename="$current_dump"
	grep -Fq Short "$current_dump"
	! grep -Fq 'New second' "$current_dump"
	replace_text ''
	"$topdir"/plainmouth action=dump id=output filename="$current_dump"
	! grep -Fq Short "$current_dump"
	"$topdir"/plainmouth action=set-value id=output node=2 "value=$text"
	"$topdir"/plainmouth action=dump id=output filename="$current_dump"
	grep -Fq 'First line' "$current_dump"
	"$topdir"/plainmouth action=set-value id=output node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=output)" = "$(printf 'BUTTON_4=0\nBUTTON_5=1')"
	"$topdir"/plainmouth action=delete id=output
	expect_error action=create plugin=compose id=bad width=32 height=8 \
		node=vbox node=textview text=parent node=label text=child node=end node=end \
		node=button text=OK node=end node=end
	expect_error action=create plugin=compose id=bad width=32 height=8 \
		node=vbox node=textview node=end node=button text=OK node=end node=end
	expect_error action=create plugin=compose id=bad width=32 height=8 \
		node=vbox node=textview "text=$rectangle" node=end node=button text=OK node=end node=end
	"$topdir"/plainmouth action=create plugin=compose id=nested width=32 height=8 border=true \
		node=vbox node=scroll flex-h=1 node=vbox \
		  node=textview node-id=output text=Initial node=end \
		node=end node=end node=button node-id=ok text=OK node=end node=end
	"$topdir"/plainmouth action=set-value id=nested node-id=output "value=$text"
	"$topdir"/plainmouth action=dump id=nested filename="$current_dump"
	grep -Fq 'First line' "$current_dump"
	! grep -Fq 'Last line' "$current_dump"
	"$topdir"/plainmouth action=set-value id=nested node-id=output value=Nested
	"$topdir"/plainmouth action=dump id=nested filename="$current_dump"
	grep -Fq Nested "$current_dump"
	"$topdir"/plainmouth action=set-value id=nested node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=nested)" = 'BUTTON_5=1'
	"$topdir"/plainmouth action=delete id=nested
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump"
