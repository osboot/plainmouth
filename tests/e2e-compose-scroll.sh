#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

current_result="$testsdir/$progname.result"

draw_testcase()
{
	local nodes=(node=vbox node=label text=Settings node=end
		node=scroll flex-h=1 node=vbox) i

	for ((i = 1; i <= 10; i++)); do
		nodes+=(node=hbox node=label "text=Field $i: " node=end
			node=input "value=value$i" flex-w=1 node=end node=end)
	done

	nodes+=(node=end node=end node=hbox
		node=button text=OK node=end node=button text=Cancel node=end
		node=end node=end)
	"$topdir"/plainmouth action=create plugin=compose id=scroll \
		width=32 height=9 border=true "${nodes[@]}"
}

expect_create_error()
{
	local actual status=0
	actual=$("$topdir"/plainmouth action=create plugin=compose id=bad \
		width=32 height=9 border=true "$@") || status=$?
	test "$status" -eq 1

	case "$actual" in
		*"ERR=compose scroll content exceeds"*) ;;
		*) return 1 ;;
	esac
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=scroll
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	rm -f -- "$current_dump.initial" "$current_dump.top" "$current_dump.wide" "$current_dump.nested"
	draw_testcase
	"$topdir"/plainmouth action=dump id=scroll filename="$current_dump.initial"
	"$topdir"/plainmouth action=set-value id=scroll node=34 value=changed
	"$topdir"/plainmouth action=dump id=scroll filename="$current_dump"
	"$topdir"/plainmouth action=set-value id=scroll node=7 value=first
	"$topdir"/plainmouth action=dump id=scroll filename="$current_dump.top"
	"$topdir"/plainmouth action=set-value id=scroll node=36 clicked=true
	"$topdir"/plainmouth action=wait-result id=scroll > "$current_result"
	"$topdir"/plainmouth action=delete id=scroll

	"$topdir"/plainmouth action=create plugin=compose id=wide width=32 height=6 border=true \
		node=vbox \
		  node=scroll flex-h=1 \
		    node=hbox \
		      node=label text=0123456789012345678901234567890123456789 node=end \
		      node=input value= flex-w=1 node=end \
		    node=end \
		  node=end \
		  node=button text=OK node=end \
		node=end
	"$topdir"/plainmouth action=set-value id=wide node=5 value=Z
	"$topdir"/plainmouth action=dump id=wide filename="$current_dump.wide"
	"$topdir"/plainmouth action=delete id=wide

	# Internal scroll widgets must not interfere with declared IDs or results.
	"$topdir"/plainmouth action=create plugin=compose id=nested width=32 height=6 border=true \
		node=vbox node=scroll flex-h=1 node=vbox node=scroll flex-h=1 node=vbox \
		  node=input value=first node=end \
		  node=input value=last node=end \
		node=end node=end node=end node=end \
		node=button text=OK node=end node=end
	"$topdir"/plainmouth action=set-value id=nested node=7 value=nested
	test "$("$topdir"/plainmouth action=result id=nested)" = "$(printf 'INPUT_6=first\nINPUT_7=nested\nBUTTON_8=0')"
	"$topdir"/plainmouth action=dump id=nested filename="$current_dump.nested"
	grep -Fq nested "$current_dump.nested"
	"$topdir"/plainmouth action=delete id=nested

	local long_text tall_text
	printf -v long_text '%4096s' ''
	long_text=${long_text// /x}
	expect_create_error node=vbox node=scroll node=hbox \
		node=label "text=$long_text" node=end node=label text=x node=end \
		node=end node=end node=button text=OK node=end node=end
	# Each axis fits its bound, but the backing pad would exceed the cell limit.
	printf -v tall_text '%300s' ''
	tall_text=${tall_text// /$'\n'}x
	expect_create_error node=vbox node=scroll node=vbox \
		node=label "text=$long_text" node=end node=label "text=$tall_text" node=end \
		node=end node=end node=button text=OK node=end node=end

	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"

case "$MODE" in
	view) run_test testcase_view & ;;
	dump) run_test testcase_dump & ;;
esac

test_pid=$!
run_server "$test_pid"

if [ "$MODE" = dump ]; then
	verify_dump "$current_dump"
	diff -u "$testsdir/expected-e2e-compose-scroll.initial" "$current_dump.initial"
	diff -u "$testsdir/expected-e2e-compose-scroll.top" "$current_dump.top"
	diff -u "$testsdir/expected-e2e-compose-scroll.wide" "$current_dump.wide"
	diff -u "$testsdir/expected-e2e-compose-scroll.result" "$current_result"
fi

clear_testdata "$current_dump" "$current_dump.initial" "$current_dump.top" \
	"$current_dump.wide" "$current_dump.nested" "$current_result"
