#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=layout width=36 height=12 border=true \
		node=vbox gap=1 \
		  node=label text=Layout node=end \
		  node=hbox gap=2 \
		    node=label text=Host: node=end \
		    node=input node-id=host value=localhost flex-w=1 node=end \
		  node=end \
		  node=scroll flex-h=1 \
		    node=vbox gap=1 \
		      node=label text=First node=end \
		      node=spacer height=3 node=end \
		      node=hbox gap=2 \
		        node=label text=Last: node=end \
		        node=input node-id=last value=bottom flex-w=1 node=end \
		      node=end \
		    node=end \
		  node=end \
		  node=hbox gap=2 \
		    node=spacer flex-w=1 node=end \
		    node=button node-id=ok text=OK node=end \
		    node=button text=Cancel node=end \
		  node=end \
		node=end
}

expect_create_error()
{
	local actual status=0
	actual=$("$topdir"/plainmouth action=create plugin=compose id=bad width=36 height=12 border=true "$@") || status=$?
	test "$status" -eq 1

	case "$actual" in
		ERR=*) ;;
		*) return 1 ;;
	esac
}

testcase()
{
	if [ "$MODE" = dump ]; then
		rm -f -- "$current_dump.initial"
	fi

	draw_testcase

	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=layout
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=layout filename="$current_dump.initial"
	"$topdir"/plainmouth action=set-value id=layout node-id=host value=example.org
	"$topdir"/plainmouth action=set-value id=layout node-id=last value=changed
	"$topdir"/plainmouth action=dump id=layout filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=layout node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=layout)" = "$(printf 'INPUT_5=example.org\nINPUT_12=changed\nBUTTON_15=1\nBUTTON_16=0')"

	expect_create_error node=vbox gap=-1 node=button text=OK node=end node=end
	expect_create_error node=vbox gap=4097 node=button text=OK node=end node=end
	expect_create_error node=vbox gap=bad node=button text=OK node=end node=end
	expect_create_error node=vbox gap=1 gap=2 node=button text=OK node=end node=end
	expect_create_error node=vbox node=button text=OK gap=1 node=end node=end
	expect_create_error node=vbox node=spacer width=-1 node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer height=bad node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer width=4097 node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer height=4097 node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer width=1 width=2 node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer height=1 node=label text=bad node=end node=end node=button text=OK node=end node=end
	expect_create_error node=vbox gap=12 node=label text=Top node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=spacer width=36 node=end node=button text=OK node=end node=end
	expect_create_error node=vbox node=scroll node=vbox gap=4096 \
		node=label text=x node=end node=label text=y node=end \
		node=end node=end node=button text=OK node=end node=end
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"

if [ "$MODE" = dump ]; then
	verify_dump "$current_dump"
	diff -u "$testsdir/expected-e2e-compose-layout.initial" "$current_dump.initial"
fi

clear_testdata "$current_dump" "$current_dump.initial"
