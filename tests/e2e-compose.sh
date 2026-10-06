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
	"$topdir"/plainmouth --quit
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
