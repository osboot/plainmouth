#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

create_dialog()
{
	local plugin=$1
	shift
	"$topdir"/plainmouth action=create plugin="$plugin" id=names \
		width=40 height=12 border=true text='Named widgets' \
		button=Continue button=Leave "$@"
}

focus_names()
{
	local name

	for name; do
		"$topdir"/plainmouth action=focus id=names node-id="$name"
	done
}

expect_error()
{
	local expected=$1 actual status=0
	shift
	actual=$("$topdir"/plainmouth "$@") || status=$?
	test "$status" -eq 1
	test "$actual" = "ERR=$expected"
}

testcase()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT

	if [ "$MODE" = view ]; then
		create_dialog formbox \
			hbox=start label=Host: input=localhost hbox=end \
			hbox=start label=Password: password=secret hbox=end
		focus_names input2
		"$topdir"/plainmouth action=wait-result id=names
		return
	fi

	local plugin

	for plugin in msgbox inputbox passwordbox menubox checklistbox timebox rangebox formbox; do
		local args=() names=(text button1 button2)

		case "$plugin" in
			inputbox|passwordbox)
				args=(value=initial)
				names+=(input)
				;;
			menubox|checklistbox)
				args=(visible=3 option=One option=Two)
				names+=(choices)
				;;
			timebox)
				names+=(hour minute second)
				;;
			rangebox)
				args=(min=0 max=100 value=50)
				names+=(value)
				;;
			formbox)
				args=(hbox=start label=Name: input=initial password=secret hbox=end)
				names+=(input1 input2)
				;;
		esac

		create_dialog "$plugin" "${args[@]}"
		focus_names "${names[@]}"
		expect_error 'unsupported focus parameter: node' action=focus id=names node=1
		expect_error 'duplicate focus parameter: node-id' action=focus id=names node-id=button1 node-id=button2
		expect_error 'invalid node-id' action=focus id=names node-id=
		expect_error 'invalid node-id' action=focus id=names 'node-id=bad name'
		expect_error 'node not found' action=focus id=names node-id=button3
		"$topdir"/plainmouth action=delete id=names
	done

	for plugin in textbox tailbox; do
		create_dialog "$plugin" file="$testsdir/expected-e2e-textbox"
		focus_names text button1
		expect_error 'node not found' action=focus id=names node-id=button2
		"$topdir"/plainmouth action=delete id=names
	done

	if [ -f "$topdir/plugins/termbox.so" ]; then
		create_dialog termbox command='printf ready'
		focus_names terminal button1
		expect_error 'node not found' action=focus id=names node-id=text
		"$topdir"/plainmouth action=delete id=names
	fi

	for plugin in meterbox gaugebox; do
		create_dialog "$plugin" total=100 value=25 label='Named widgets'
		expect_error 'node not found or not focusable' action=focus id=names node-id=meter
		expect_error 'node not found or not focusable' action=focus id=names node-id=text
		"$topdir"/plainmouth action=delete id=names
	done

	"$topdir"/plainmouth action=create plugin=inputbox id=other width=20 height=4 value=other
	"$topdir"/plainmouth action=create plugin=formbox id=names width=30 height=8 border=true layout=positioned \
		field=start input=first x=0 y=0 width=10 field=end \
		field=start label=Label x=12 y=0 width=5 field=end \
		field=start password=secret x=0 y=1 width=10 field=end \
		field=start input=locked readonly=true x=0 y=2 width=10 field=end \
		field=start input=disabled disabled=true x=0 y=3 width=10 field=end \
		button=Continue button=Leave
	focus_names input1 input2 button2
	"$topdir"/plainmouth action=dump id=names filename="$current_dump"
	expect_error 'node not found' action=focus id=names node-id=input
	expect_error 'node not found' action=focus id=other node-id=input2
	expect_error 'node not found' action=focus id=other node-id=text
	expect_error 'node not found or not focusable' action=focus id=names node-id=input3
	expect_error 'node not found or not focusable' action=focus id=names node-id=input4
	"$topdir"/plainmouth action=dump id=names filename="$current_dump.after"
	cmp "$current_dump" "$current_dump.after"
	"$topdir"/plainmouth action=delete id=names
	"$topdir"/plainmouth action=delete id=other
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.after"
