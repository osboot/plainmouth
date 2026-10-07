#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_value()
{
	local expected=$1
	shift
	test "$("$topdir"/plainmouth action=get-value id=values "$@")" = "VALUE=$expected"
}

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
	"$topdir"/plainmouth action=create plugin=compose id=values width=32 height=16 border=true \
		node=vbox \
		  node=input node-id=host value=localhost node=end \
		  node=password node-id=secret value=secret node=end \
		  node=checkbox node-id=check notify=true node=end \
		  node=select node-id=mode notify=true option=One option=Two node=end \
		  node=spinbox node-id=count min=-10 max=10 value=0 notify=true node=end \
		  node=meter node-id=progress value=25 node=end \
		  node=spinner node-id=busy active=false node=end \
		  node=button node-id=test text=Test close=false node=end \
		  node=button node-id=ok text=OK node=end \
		node=end

	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=values
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=values filename="$current_dump"
	expect_value localhost node-id=host
	expect_value secret node=3
	expect_value 0 node-id=check
	expect_value 1 node-id=mode
	expect_value 0 node-id=count
	expect_value 25 node-id=progress
	expect_value 0 node-id=busy
	expect_value 0 node-id=test
	"$topdir"/plainmouth action=dump id=values filename="$current_dump.after"
	cmp "$current_dump" "$current_dump.after"
	"$topdir"/plainmouth action=set-value id=values node-id=host value='a b=c'
	expect_value 'a b=c' node=2
	"$topdir"/plainmouth action=set-value id=values node-id=host value=
	expect_value '' node-id=host
	"$topdir"/plainmouth action=update id=values node-id=count disabled=true readonly=true
	"$topdir"/plainmouth action=set-value id=values node-id=count value=-7
	expect_value -7 node-id=count
	"$topdir"/plainmouth action=set-value id=values node-id=check checked=true
	"$topdir"/plainmouth action=set-value id=values node-id=mode value=2
	"$topdir"/plainmouth action=set-value id=values node-id=progress value=75
	expect_value 1 node-id=check
	expect_value 2 node-id=mode
	expect_value 75 node-id=progress
	"$topdir"/plainmouth action=set-value id=values node-id=busy active=true
	expect_value 1 node-id=busy
	"$topdir"/plainmouth action=set-value id=values node-id=busy active=false
	expect_value 0 node-id=busy
	"$topdir"/plainmouth action=set-value id=values node-id=test clicked=true
	expect_value 0 node-id=test
	test "$("$topdir"/plainmouth action=wait-event id=values)" = \
		"$(printf 'EVENT=button\nNODE=9\nNODE_ID=test')"
	expect_error action=get-value id=missing node=2
	expect_error action=get-value node=2
	expect_error action=get-value id=values
	expect_error action=get-value id=values node=2 node-id=host
	expect_error action=get-value id=values node=2 node=2
	expect_error action=get-value id=values node-id=host node-id=host
	expect_error action=get-value id=values node=999
	expect_error action=get-value id=values node=0
	expect_error action=get-value id=values node=bad
	expect_error action=get-value id=values node-id=unknown
	expect_error action=get-value id=values node=1
	expect_error action=get-value id=values node=2 value=bad
	"$topdir"/plainmouth action=set-value id=values node-id=ok clicked=true
	expect_value 1 node-id=ok
	expect_value -7 node-id=count
	expect_error action=wait-event id=values
	"$topdir"/plainmouth action=delete id=values
	"$topdir"/plainmouth action=create plugin=msgbox id=unsupported width=20 height=5 text=Test button=OK
	expect_error action=get-value id=unsupported node=1
	"$topdir"/plainmouth action=delete id=unsupported
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.after"
