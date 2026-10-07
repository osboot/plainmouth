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
	"$topdir"/plainmouth action=create plugin=compose id=numbers width=36 height=9 border=true \
		node=vbox gap=1 \
		  node=hbox \
		    node=label text='Retries: ' node=end \
		    node=spinbox node-id=retries min=0 max=10 step=2 value=3 node=end \
		  node=end \
		  node=scroll flex-h=1 \
		    node=vbox \
		      node=hbox \
		        node=label text='Signed: ' node=end \
		        node=spinbox node-id=signed min=-2147483648 max=2147483647 node=end \
		      node=end \
		      node=spacer height=5 node=end \
		      node=hbox \
		        node=label text='Offset: ' node=end \
		        node=spinbox node-id=offset min=-100 max=100 step=5 value=-10 node=end \
		      node=end \
		    node=end \
		  node=end \
		  node=button node-id=ok text=OK node=end \
		node=end

	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=numbers
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=numbers filename="$current_dump"
	local before after
	before=$("$topdir"/plainmouth action=result id=numbers)
	expect_error action=set-value id=numbers node-id=retries value=-1
	expect_error action=set-value id=numbers node-id=retries value=11
	expect_error action=set-value id=numbers node-id=retries value=bad
	expect_error action=set-value id=numbers node-id=retries
	expect_error action=set-value id=numbers node-id=retries value=1 value=2
	expect_error action=set-value id=numbers node-id=retries min=0 value=1
	expect_error action=set-value id=numbers node-id=signed value=2147483648
	after=$("$topdir"/plainmouth action=result id=numbers)
	test "$before" = "$after"
	"$topdir"/plainmouth action=set-value id=numbers node-id=retries value=10
	"$topdir"/plainmouth action=set-value id=numbers node=9 value=2147483647
	"$topdir"/plainmouth action=dump id=numbers filename="$current_dump"
	"$topdir"/plainmouth action=update id=numbers node-id=offset readonly=true
	"$topdir"/plainmouth action=set-value id=numbers node-id=offset value=-100
	"$topdir"/plainmouth action=dump id=numbers filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=numbers node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=numbers)" = "$(printf 'SPINBOX_4=10\nSPINBOX_9=2147483647\nSPINBOX_13=-100\nBUTTON_14=1')"
	"$topdir"/plainmouth action=delete id=numbers

	for properties in 'min=bad' 'min=11 max=10' 'max=-1' 'max=2147483648' \
		'step=0' 'step=-1' 'step=bad' 'value=-1' 'value=101' \
		'min=0 min=1' 'max=1 max=2' 'step=1 step=2' 'value=1 value=2'; do
		expect_error action=create plugin=compose id=bad width=36 height=9 \
			node=vbox node=spinbox $properties node=end node=button text=OK node=end node=end
	done

	"$topdir"/plainmouth action=create plugin=compose id=fixed width=36 height=9 \
		node=vbox node=spinbox min=7 max=7 node=end node=button text=OK node=end node=end
	test "$("$topdir"/plainmouth action=result id=fixed)" = "$(printf 'SPINBOX_2=7\nBUTTON_3=0')"
	"$topdir"/plainmouth action=delete id=fixed
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
verify_dump "$current_dump"
clear_testdata "$current_dump"
