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

testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=changes width=36 height=12 border=true \
		node=vbox \
		  node=hbox node=checkbox node-id=tls notify=true node=end \
		    node=label text=' Use TLS' node=end node=end \
		  node=select node-id=mode notify=true option=Fast option=Balanced option=Thorough node=end \
		  node=spinbox node-id=retries min=0 max=10 value=3 notify=true node=end \
		  node=textview node-id=status text='Waiting for changes' flex-h=1 node=end \
		  node=button node-id=ok text=OK node=end \
		node=end

	if [ "$MODE" = view ]; then
		local event

		while event=$("$topdir"/plainmouth action=wait-event id=changes); do
			"$topdir"/plainmouth action=set-value id=changes node-id=status "value=$event"
		done

		"$topdir"/plainmouth action=wait-result id=changes
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=set-value id=changes node-id=tls checked=true
	"$topdir"/plainmouth action=set-value id=changes node-id=mode value=2
	"$topdir"/plainmouth action=set-value id=changes node-id=retries value=7
	expect_error action=update id=changes node-id=tls notify=false
	"$topdir"/plainmouth action=set-value id=changes node-id=ok clicked=true
	local actual status=0
	actual=$("$topdir"/plainmouth action=wait-event id=changes) || status=$?
	test "$status" -eq 1
	test "$actual" = 'ERR=instance finished'
	test "$("$topdir"/plainmouth action=wait-result id=changes)" = \
		"$(printf 'CHECKBOX_3=1\nSELECT_5=2\nSPINBOX_6=7\nBUTTON_8=1')"
	"$topdir"/plainmouth action=delete id=changes

	for type in checkbox select spinbox; do
		expect_error action=create plugin=compose id=bad width=36 height=12 \
			node=vbox "node=$type" notify=maybe node=end node=button text=OK node=end node=end
		expect_error action=create plugin=compose id=bad width=36 height=12 \
			node=vbox "node=$type" notify=true notify=false node=end node=button text=OK node=end node=end
	done

	expect_error action=create plugin=compose id=bad width=36 height=12 \
		node=vbox node=input value=secret notify=true node=end node=button text=OK node=end node=end
	expect_error action=create plugin=compose id=bad width=36 height=12 \
		node=vbox node=password value=secret notify=true node=end node=button text=OK node=end node=end
	expect_error action=create plugin=compose id=bad width=36 height=12 \
		node=vbox notify=true node=button text=OK node=end node=end
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump"
