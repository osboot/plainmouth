#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

wait_dump()
{
	local n
	for n in {1..100}; do
		rm -f "$current_dump"
		if "$topdir"/plainmouth action=dump id="$id" filename="$current_dump" 2>/dev/null &&
		   grep -Fq "$1" "$current_dump"; then
			return
		fi
		sleep 0.01
	done
	return 1
}

testcase()
{
	local fifo="$current_dump.fifo" pid id status
	trap 'if [ -n "${pid:-}" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi; rm -f "$current_dump.fifo"; "$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	if [ "$MODE" = view ]; then
		{
			sleep 1
			printf '25\n'
			sleep 1
			printf 'XXX\n60\nCopying files\nSecond stage\nXXX\n'
			sleep 2
			printf '100\n'
			sleep 2
		} | "$topdir"/plaindialog --gauge Starting 7 40 10
		return
	fi
	"$topdir"/plainmouth action=create plugin=gaugebox id=direct width=40 height=7 \
		border=true text=Direct value=10
	if "$topdir"/plainmouth action=update id=direct value=-1 text=Invalid; then return 1; fi
	if "$topdir"/plainmouth action=set-value id=direct value=bad; then return 1; fi
	"$topdir"/plainmouth action=set-value id=direct value=100 text=Complete
	"$topdir"/plainmouth action=update id=direct value=5 text=Restarted
	id=direct
	wait_dump '5%'
	grep -Fq Restarted "$current_dump"
	"$topdir"/plainmouth action=delete id=direct
	mkfifo "$fifo"
	exec 3<>"$fifo"
	"$topdir"/plaindialog --stdout --gauge Starting 7 40 10 <"$fifo" 3>&- >"$current_dump.out" &
	pid=$! id=plaindialog-$!
	wait_dump '10%'
	grep -Fq Starting "$current_dump"
	printf '25\n' >&3
	wait_dump '25%'
	printf 'XXX\n40\nCopying files\nSecond stage\nXXX\n' >&3
	wait_dump '40%'
	grep -Fq 'Copying files' "$current_dump"
	grep -Fq 'Second stage' "$current_dump"
	printf '100\n' >&3
	wait_dump '100%'
	printf 'XXX\n70\nOne\nTwo\nThree\nFour\nFive\nSix\nSeven\nEight\nXXX\n' >&3
	wait_dump '70%'
	grep -Fq One "$current_dump"
	test "$(wc -l <"$current_dump")" = 9
	printf 'XXX\n60\nDone\nXXX\n' >&3
	wait_dump '60%'
	grep -Fq Done "$current_dump"
	if grep -Eq 'Second stage|One|Two|Three|Four' "$current_dump"; then return 1; fi
	exec 3>&-
	wait "$pid"
	pid=
	test ! -s "$current_dump.out"
	if "$topdir"/plainmouth action=result id="$id"; then return 1; fi
	printf '50' | "$topdir"/plaindialog --gauge Final 7 40
	printf 'XXX\r\n50\r\nUpdated\r\nXXX\r\n' | "$topdir"/plaindialog --gauge CRLF 7 40
	"$topdir"/plaindialog --gauge Empty 7 40 </dev/null
	if printf 'XXX\n50\nincomplete\n' | "$topdir"/plaindialog --gauge Bad 7 40; then return 1; fi
	if printf '101\n' | "$topdir"/plaindialog --gauge Bad 7 40; then return 1; fi
	if printf '5\0000\n' | "$topdir"/plaindialog --gauge Bad 7 40; then return 1; fi
	if "$topdir"/plaindialog --gauge Bad 7 40 -1 </dev/null; then return 1; fi
	if { printf 'XXX\n50\n'; printf '%9000s\n' ''; printf 'XXX\n'; } |
	   "$topdir"/plaindialog --gauge Bad 7 40; then return 1; fi
	exec 3<>"$fifo"
	"$topdir"/plaindialog --gauge Signal 7 40 <"$fifo" 3>&- &
	pid=$! id=plaindialog-$!
	wait_dump Signal
	kill -TERM "$pid"
	status=0
	wait "$pid" || status=$?
	pid=
	test "$status" = 255
	exec 3>&-
	if "$topdir"/plainmouth action=result id="$id"; then return 1; fi
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server
wait "$test_pid"
clear_testdata "$current_dump" "$current_dump.out"
