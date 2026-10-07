#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test
export LC_ALL=C.UTF-8

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

set_active()
{
	"$topdir"/plainmouth action=set-value id=spinner node-id="$1" active="$2"
}

sample()
{
	rm -f -- "$current_dump.frame"
	"$topdir"/plainmouth action=dump id=spinner filename="$current_dump.frame"
	sed -n 's/.*│\(.\)  ASCII.*/\1/p' "$current_dump.frame"
}

testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=spinner width=40 height=13 border=true \
		node=vbox gap=1 \
		  node=label node-id=status text='Ready to start' node=end \
		  node=hbox gap=2 \
		    node=spinner node-id=ascii frames=ascii node=end \
		    node=label text=ASCII node=end \
		  node=end \
		  node=hbox gap=2 \
		    node=spinner node-id=braille frames=braille node=end \
		    node=label text=Braille node=end \
		  node=end \
		  node=hbox gap=2 \
		    node=spinner node-id=wave frames=wave node=end \
		    node=label text=Wave node=end \
		  node=end \
		  node=meter node-id=progress node=end \
		  node=hbox gap=2 \
		    node=spacer flex-w=1 node=end \
		    node=button node-id=start text=Start close=false node=end \
		    node=button node-id=ok text=OK node=end \
		  node=end \
		node=end

	if [ "$MODE" = view ]; then
		while "$topdir"/plainmouth action=wait-event id=spinner >/dev/null; do
			"$topdir"/plainmouth action=set-value id=spinner node-id=status text='Working...'

			for name in ascii braille wave; do
				set_active "$name" true
			done

			for value in 0 10 20 30 40 50 60 70 80 90 100; do
				"$topdir"/plainmouth action=set-value id=spinner node-id=progress value="$value"
				sleep 0.3
			done

			for name in ascii braille wave; do
				set_active "$name" false
			done

			"$topdir"/plainmouth action=set-value id=spinner node-id=status text=Done
		done

		"$topdir"/plainmouth action=wait-result id=spinner
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=spinner filename="$current_dump"
	set_active ascii true
	local initial current changed=false
	initial=$(sample)

	case "$initial" in
		'|'|'/'|'-'|'\') ;;
		*) return 1 ;;
	esac

	for attempt in {1..40}; do
		current=$(sample)

		if [ "$current" != "$initial" ]; then
			changed=true
			break
		fi

		sleep 0.05
	done

	test "$changed" = true
	expect_error action=set-value id=spinner node-id=ascii active=bad
	expect_error action=set-value id=spinner node-id=ascii
	expect_error action=set-value id=spinner node-id=ascii active=true active=false
	expect_error action=set-value id=spinner node-id=ascii frames=wave active=true
	"$topdir"/plainmouth action=set-value id=spinner node=4 active=false
	test "$(sample)" = ' '
	diff -u "$current_dump" "$current_dump.frame"
	sleep 0.2
	test "$(sample)" = ' '
	set_active braille true
	set_active wave true
	set_active braille false
	set_active wave false
	"$topdir"/plainmouth action=set-value id=spinner node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=spinner)" = "$(printf 'BUTTON_15=0\nBUTTON_16=1')"
	"$topdir"/plainmouth action=delete id=spinner

	for properties in 'frames=bad' 'active=bad' 'frames=ascii frames=wave' 'active=true active=false'; do
		expect_error action=create plugin=compose id=bad width=20 height=5 \
			node=vbox node=spinner $properties node=end node=button text=OK node=end node=end
	done

	expect_error action=create plugin=compose id=bad width=20 height=5 \
		node=vbox node=spinner node=label text=bad node=end node=end node=button text=OK node=end node=end
	"$topdir"/plainmouth action=create plugin=compose id=running width=20 height=5 \
		node=vbox node=spinner active=true frames=ascii node=end node=button text=OK node=end node=end
	"$topdir"/plainmouth action=delete id=running
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
verify_dump "$current_dump"
clear_testdata "$current_dump" "$current_dump.frame"
