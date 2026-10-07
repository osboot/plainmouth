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

set_progress()
{
	"$topdir"/plainmouth action=set-value id=progress node-id=meter value="$1"
}

testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=progress width=36 height=9 border=true \
		node=vbox gap=1 \
		  node=label node-id=status text='Ready to start' node=end \
		  node=meter node-id=meter node=end \
		  node=hbox gap=2 \
		    node=spacer flex-w=1 node=end \
		    node=button node-id=start text=Start close=false node=end \
		    node=button node-id=ok text=OK node=end \
		  node=end \
		node=end

	if [ "$MODE" = view ]; then
		while "$topdir"/plainmouth action=wait-event id=progress >/dev/null; do
			"$topdir"/plainmouth action=set-value id=progress node-id=status text='Working...'

			for value in 0 10 20 30 40 50 60 70 80 90 100; do
				set_progress "$value"
				sleep 0.1
			done

			"$topdir"/plainmouth action=set-value id=progress node-id=status text='Done'
		done

		"$topdir"/plainmouth action=wait-result id=progress
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=progress filename="$current_dump"
	set_progress 50
	expect_error action=set-value id=progress node-id=meter value=-1
	expect_error action=set-value id=progress node-id=meter value=101
	expect_error action=set-value id=progress node-id=meter value=bad
	expect_error action=set-value id=progress node-id=meter
	expect_error action=set-value id=progress node-id=meter value=1 value=2
	expect_error action=set-value id=progress node-id=meter total=200 value=1
	"$topdir"/plainmouth action=dump id=progress filename="$current_dump"
	set_progress 100
	"$topdir"/plainmouth action=dump id=progress filename="$current_dump"
	set_progress 0
	"$topdir"/plainmouth action=set-value id=progress node=3 value=25
	"$topdir"/plainmouth action=dump id=progress filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=progress node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=progress)" = "$(printf 'BUTTON_6=0\nBUTTON_7=1')"
	"$topdir"/plainmouth action=delete id=progress

	for properties in 'total=0' 'total=-1' 'total=bad' 'total=2147483648' \
		'value=-1' 'value=101' 'value=bad' 'total=1 total=2' 'value=1 value=2'; do
		expect_error action=create plugin=compose id=bad width=36 height=9 \
			node=vbox node=meter $properties node=end node=button text=OK node=end node=end
	done

	"$topdir"/plainmouth action=create plugin=compose id=large width=36 height=9 border=true \
		node=vbox node=meter node-id=meter total=2147483647 value=2147483647 node=end \
		node=button text=OK node=end node=end
	"$topdir"/plainmouth action=set-value id=large node-id=meter value=1073741823
	"$topdir"/plainmouth action=delete id=large
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
verify_dump "$current_dump"
clear_testdata "$current_dump"
