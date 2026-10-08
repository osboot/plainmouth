#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_error()
{
	local actual status=0
	actual=$("$topdir"/plainmouth action=create plugin=compose id=bad width=30 height=8 "$@") || status=$?
	test "$status" -eq 1
	[[ "$actual" = ERR=* ]]
}

testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=groups width=38 height=16 border=true \
		node=vbox border=true label=Settings node-id=group padding=1 \
		  node=hbox border=true label=Connection gap=1 padding=2 padding-y=0 \
		    node=label text=Host: node=end \
		    node=input node-id=host value=localhost flex-w=1 node=end \
		  node=end \
		  node=scroll flex-h=1 \
		    node=vbox border=true padding-x=2 padding=1 label='A caption much longer than the available width' \
		      node=label text=First node=end \
		      node=spacer height=5 node=end \
		      node=input node-id=last value=bottom node=end \
		    node=end \
		  node=end \
		  node=button node-id=ok text=OK node=end \
		node=end

	if [ "$MODE" = view ]; then
		"$topdir"/plainmouth action=wait-result id=groups
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=dump id=groups filename="$current_dump.initial"
	grep -q Settings "$current_dump.initial"
	grep -q Connection "$current_dump.initial"
	grep -q 'A caption much' "$current_dump.initial"
	"$topdir"/plainmouth action=focus id=groups node-id=last
	"$topdir"/plainmouth action=set-value id=groups node=9 value=changed
	"$topdir"/plainmouth action=dump id=groups filename="$current_dump"
	grep -q changed "$current_dump"
	! grep -q 'A caption much' "$current_dump"
	"$topdir"/plainmouth action=update id=groups node-id=group disabled=true
	! "$topdir"/plainmouth action=focus id=groups node-id=last
	"$topdir"/plainmouth action=update id=groups node-id=group disabled=false readonly=true
	! "$topdir"/plainmouth action=focus id=groups node-id=host
	"$topdir"/plainmouth action=update id=groups node-id=group readonly=false
	"$topdir"/plainmouth action=focus id=groups node-id=host
	"$topdir"/plainmouth action=set-value id=groups node-id=ok clicked=true
	test "$("$topdir"/plainmouth action=wait-result id=groups)" = "$(printf 'INPUT_4=localhost\nINPUT_9=changed\nBUTTON_10=1')"

	expect_error node=vbox label=bad node=button text=OK node=end node=end
	expect_error node=vbox border=bad node=button text=OK node=end node=end
	expect_error node=vbox border=true border=true node=button text=OK node=end node=end
	expect_error node=vbox border=true label=one label=two node=button text=OK node=end node=end
	expect_error node=vbox border=true label=$'bad\ncaption' node=button text=OK node=end node=end
	expect_error node=vbox node=input value=x border=true node=end node=button text=OK node=end node=end
	expect_error node=vbox border=true node=spacer width=30 node=end node=button text=OK node=end node=end
	expect_error node=vbox padding=-1 node=button text=OK node=end node=end
	expect_error node=vbox padding-x=4097 node=button text=OK node=end node=end
	expect_error node=vbox padding-y=bad node=button text=OK node=end node=end
	expect_error node=vbox padding=1 padding=2 node=button text=OK node=end node=end
	expect_error node=vbox node=input value=x padding=1 node=end node=button text=OK node=end node=end
	expect_error node=vbox padding=15 node=button text=OK node=end node=end
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.initial"
