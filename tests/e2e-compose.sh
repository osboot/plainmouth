#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

draw_testcase()
{
	"$topdir"/plainmouth action=create plugin=compose id=connection style=connection \
		width=40 height=12 border=true \
		node=vbox \
		  node=label text="Connection" node=end \
		  node=hbox \
		    node=label text="Host: " node=end \
		    node=input value=localhost max-length=32 flex-w=1 node=end \
		  node=end \
		  node=hbox \
		    node=label text="Port: " node=end \
		    node=input value=22 max-length=5 flex-w=1 node=end \
		  node=end \
		  node=hbox \
		    node=checkbox checked=false node=end \
		    node=label text=" Use TLS" node=end \
		  node=end \
		  node=hbox \
		    node=label text="Password: " node=end \
		    node=password value=secret flex-w=1 node=end \
		  node=end \
		  node=select visible=3 value=1 \
			option=SSH option=HTTP option=HTTPS option=FTP \
			option=SMTP option=Custom node=end \
		  node=hbox \
		    node=button text=OK node=end \
		    node=button text=Cancel node=end \
		  node=end \
		node=end
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

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=connection
	"$topdir"/plainmouth action=delete id=connection
	"$topdir"/plainmouth action=create plugin=compose id=probe width=36 height=7 border=true \
		node=vbox \
		  node=label node-id=status text="Waiting for connection..." node=end \
		  node=button node-id=test text="Test connection" close=false node=end \
		  node=hbox \
		    node=button text=OK node=end \
		    node=button text=Cancel node=end \
		  node=end \
		node=end
	local event count=0

	while event=$("$topdir"/plainmouth action=wait-event id=probe); do
		test "$event" = "$(printf 'EVENT=button\nNODE=3\nNODE_ID=test')"
		count=$((count + 1))
		"$topdir"/plainmouth action=set-value id=probe node-id=status text="Connected ($count)"
	done

	test "$event" = 'ERR=instance finished'
	"$topdir"/plainmouth action=wait-result id=probe
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	rm -f -- "$current_dump.initial" "$current_result"
	draw_testcase
	"$topdir"/plainmouth action=dump id=connection filename="$current_dump.initial"
	"$topdir"/plainmouth action=result id=connection > "$current_result"
	local before after
	before=$("$topdir"/plainmouth action=result id=connection)
	expect_error action=set-value id=connection node=5 value=bad checked=true
	expect_error action=set-value id=connection node=5 value=one value=two
	expect_error action=set-value id=connection node=5 value=abcdefghijklmnopqrstuvwxyz1234567890
	expect_error action=set-value id=connection node=10
	expect_error action=set-value id=connection node=10 checked=maybe
	expect_error action=set-value id=connection node=15 value=99
	expect_error action=set-value id=connection node=15 value=-2147483648
	expect_error action=set-value id=connection node=2 value=text
	expect_error action=set-value id=connection node=999 value=text
	after=$("$topdir"/plainmouth action=result id=connection)
	test "$before" = "$after"

	"$topdir"/plainmouth action=set-value id=connection node=5 value=example.org
	"$topdir"/plainmouth action=set-value id=connection node=8 value=443
	"$topdir"/plainmouth action=set-value id=connection node=10 checked=true
	"$topdir"/plainmouth action=set-value id=connection node=14 value=
	"$topdir"/plainmouth action=set-value id=connection node=15 value=6
	"$topdir"/plainmouth action=dump id=connection filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=connection node=17 clicked=true
	"$topdir"/plainmouth action=wait-result id=connection >> "$current_result"

	local prefix=(action=create plugin=compose id=bad width=40 height=12)
	expect_error "${prefix[@]}"
	expect_error "${prefix[@]}" node=vbox
	expect_error "${prefix[@]}" node=unknown node=end
	expect_error "${prefix[@]}" node=button text=OK node=end
	expect_error "${prefix[@]}" node=vbox node=end
	expect_error "${prefix[@]}" node=vbox flex-w=1 flex-w=2 node=end
	expect_error "${prefix[@]}" node=vbox checked=true node=end
	expect_error "${prefix[@]}" node=vbox parent=0 node=end
	expect_error "${prefix[@]}" node=start type=vbox parent=0 node=end
	expect_error "${prefix[@]}" node=end
	expect_error "${prefix[@]}" node=vbox node=button text=OK node=end node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button text=OK node=end node=end node=vbox node=end
	expect_error "${prefix[@]}" node=vbox node=button text=OK node=label text=invalid node=end node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button text=OK node=end \
		node=select option=one value=2 node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button text=OK node=end flex-w=1 node=end
	expect_error "${prefix[@]}" width=bad node=vbox node=end
	expect_error "${prefix[@]}" unknown=true node=vbox node=end
	expect_error "${prefix[@]}" node=vbox node=input value=long max-length=1 node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button text=Test close=maybe node=end node=end
	expect_error "${prefix[@]}" node=vbox disabled=maybe node=button text=OK node=end node=end

	local nodes=(node=vbox) i

	for ((i = 1; i <= 256; i++)); do
		nodes+=(node=button text=OK node=end)
	done

	nodes+=(node=end)
	expect_error "${prefix[@]}" "${nodes[@]}"
	nodes=(node=vbox)

	for ((i = 1; i <= 32; i++)); do
		nodes+=(node=vbox)
	done

	for ((i = 0; i <= 32; i++)); do
		nodes+=(node=end)
	done

	expect_error "${prefix[@]}" "${nodes[@]}"

	# A rejected partial tree must not reserve the instance ID.
	"$topdir"/plainmouth "${prefix[@]}" node=vbox node=button text=OK node=end node=end
	"$topdir"/plainmouth action=delete id=bad
	testcase_events
	testcase_named_nodes
	"$topdir"/plainmouth --quit
}

testcase_events()
{
	local client="$topdir/plainmouth" actual waiter status i
	local eventfile="$current_result.event"
	"$client" action=create plugin=compose id=events width=32 height=6 \
		node=vbox \
		  node=label text="Waiting for connection..." node=end \
		  node=button text=Test close=false node=end \
		  node=button text=Retry close=false node=end \
		  node=button text=OK node=end \
		node=end
	expect_error action=update id=events node=3
	expect_error action=update id=events node=999 disabled=true
	expect_error action=update id=events node=3 disabled=true readonly=maybe
	expect_error action=update id=events node=3 disabled=true disabled=false
	expect_error action=update id=events node=3 disabled=true unknown=true
	"$client" action=update id=events node=1 disabled=true readonly=true
	"$client" action=update id=events node=1 disabled=false
	"$client" action=update id=events node=1 readonly=false
	expect_error action=set-value id=events node=2 text="This status is too long for the label"
	"$client" action=set-value id=events node=2 text=Connected
	"$client" action=dump id=events filename="$eventfile"
	grep -q Connected "$eventfile"
	! grep -q Waiting "$eventfile"
	expect_error action=wait-event

	# Clicks made before waiting remain queued, in order, including repeats.
	for i in 3 4 3; do
		"$client" action=set-value id=events node="$i" clicked=true
	done

	for i in 3 4 3; do
		actual=$("$client" action=wait-event id=events)
		test "$actual" = "$(printf 'EVENT=button\nNODE=%s' "$i")"
	done

	# An empty queue blocks; a new click wakes the client.
	"$client" action=wait-event id=events > "$eventfile" &
	waiter=$!
	sleep 0.1
	kill -0 "$waiter"
	"$client" action=set-value id=events node=4
	wait "$waiter"
	test "$(cat "$eventfile")" = "$(printf 'EVENT=button\nNODE=4')"
	actual=$("$client" action=result id=events)
	test "$actual" = "$(printf 'BUTTON_3=0\nBUTTON_4=0\nBUTTON_5=0')"
	"$client" action=set-value id=events node=5
	"$client" action=wait-result id=events > /dev/null
	expect_error action=wait-event id=events
	"$client" action=delete id=events

	"$client" action=create plugin=compose id=events width=32 height=6 \
		node=vbox node=button text=Test close=false node=end node=end
	"$client" action=wait-event id=events > "$eventfile" &
	waiter=$!
	sleep 0.1
	kill -0 "$waiter"
	"$client" action=delete id=events
	status=0
	wait "$waiter" || status=$?
	test "$status" -eq 1
	test "$(cat "$eventfile")" = 'ERR=no instance'

	"$client" action=create plugin=compose id=events width=32 height=6 \
		node=vbox node=button text=Test close=false node=end node=end

	for ((i = 0; i < 257; i++)); do
		"$client" action=set-value id=events node=2
	done

	actual=$("$client" action=wait-event id=events) && return 1
	test "$actual" = 'ERR=event queue overflow'
	"$client" action=delete id=events
	rm -f -- "$eventfile"
}

testcase_named_nodes()
{
	local client="$topdir/plainmouth" inserted number actual i
	local prefix=(action=create plugin=compose id=named width=32 height=6)
	expect_error "${prefix[@]}" node=vbox node-id=dup \
		node=button node-id=dup text=OK node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button node-id= text=OK node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button node-id="bad name" text=OK node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button node-id="$(printf '%065d' 0)" text=OK node=end node=end
	expect_error "${prefix[@]}" node=vbox node=button node-id=one node-id=two text=OK node=end node=end

	for inserted in false true; do
		local nodes=(node=vbox node-id=body)
		number=3

		if [ "$inserted" = true ]; then
			nodes+=(node=label text=Inserted node=end)
			number=4
		fi

		nodes+=(node=label node-id=status text="Waiting for connection..." node=end
			node=button node-id=test text=Test close=false node=end
			node=button node-id=ok text=OK node=end node=end)
		"$client" "${prefix[@]}" "${nodes[@]}"
		expect_error action=set-value id=named node="$number" node-id=test clicked=true
		expect_error action=update id=named node="$number" node-id=test disabled=true
		expect_error action=set-value id=named node-id=missing text=Missing
		expect_error action=update id=named node-id= disabled=true
		expect_error action=update id=named node-id=test node-id=ok disabled=true
		"$client" action=set-value id=named node-id=status text=Connected
		"$client" action=update id=named node-id=body disabled=true
		"$client" action=update id=named node-id=body disabled=false

		for i in 1 2; do
			"$client" action=set-value id=named node-id=test clicked=true
		done

		for i in 1 2; do
			actual=$("$client" action=wait-event id=named)
			test "$actual" = "$(printf 'EVENT=button\nNODE=%s\nNODE_ID=test' "$number")"
		done

		"$client" action=set-value id=named node=$((number + 1)) clicked=true
		actual=$("$client" action=wait-result id=named)
		test "$actual" = "$(printf 'BUTTON_%s=0\nBUTTON_%s=1' "$number" "$((number + 1))")"
		"$client" action=delete id=named
	done
}

testcase()
{
	"$topdir"/plainmouth action=set-style style=connection name=window attrs=normal

	case "$MODE" in
		view) testcase_view ;;
		dump) testcase_dump ;;
	esac
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"

if [ "$MODE" = dump ]; then
	verify_dump "$current_dump"
	diff -u "$testsdir/expected-e2e-compose.initial" "$current_dump.initial"
	diff -u "$testsdir/expected-e2e-compose.result" "$current_result"
fi

clear_testdata "$current_dump" "$current_dump.initial" "$current_result"
