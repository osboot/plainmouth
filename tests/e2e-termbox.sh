#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

termdir=$(mktemp -d "$testsdir/termbox.XXXXXX")
probe="$termdir/probe"

create_term()
{
	"$topdir"/plainmouth action=create plugin=termbox id="$1" \
		command="$2" width=24 height=4
}

await_result()
{
	local id="$1" field="$2"
	for ((n=0; n<100; n++)); do
		"$topdir"/plainmouth action=result id="$id" >"$termdir/result"
		if grep -qxF "$field" "$termdir/result"; then
			return 0
		fi
		sleep 0.01
	done
	cat "$termdir/result"
	return 1
}

await_text()
{
	local id="$1" text="$2"
	for ((n=0; n<100; n++)); do
		rm -f -- "$probe"
		"$topdir"/plainmouth action=dump id="$id" filename="$probe"
		if grep -Fq "$text" "$probe"; then
			return 0
		fi
		sleep 0.01
	done
	cat "$probe"
	return 1
}

await_reaped()
{
	local pid="$1"
	for ((n=0; n<100; n++)); do
		kill -0 "$pid" 2>/dev/null || return 0
		sleep 0.01
	done
	return 1
}

testcase_dump()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	create_term text "printf 'one\n\033[31mred\033[0m\nabc\bX\n'; exit 7"
	await_result text EXIT_STATUS=7
	await_result text OUTPUT_CLOSED=1
	"$topdir"/plainmouth action=dump id=text filename="$current_dump"
	# Exiting the command leaves the dialog open.
	grep -qx BUTTON_1=0 "$termdir/result"
	"$topdir"/plainmouth action=set-value id=text button=1
	"$topdir"/plainmouth action=wait-result id=text >"$termdir/result"
	grep -qx BUTTON_1=1 "$termdir/result"
	grep -qx EXIT_STATUS=7 "$termdir/result"
	local pid
	pid=$(sed -n 's/^PID=//p' "$termdir/result")
	"$topdir"/plainmouth action=delete id=text
	await_reaped "$pid"
	create_term tty 'stty size; test -t 0 && test -t 1 && test -t 2 && printf "pty ok\n"; for f in /proc/$$/fd/*; do case "${f##*/}" in 0|1|2) ;; *) if test -e "$f"; then printf "LEAK %s\n" "$f"; fi;; esac; done'
	await_result tty OUTPUT_CLOSED=1
	await_result tty RUNNING=0
	await_text tty 'pty ok'
	if grep -q LEAK "$probe"; then
		return 1
	fi
	"$topdir"/plainmouth action=dump id=tty filename="$current_dump"
	"$topdir"/plainmouth action=delete id=tty
	create_term screen 'printf "abcdef\rxy\033[K\n\033[31;1mRED\033[0m\033[2;10H!\033[3;1Hlast"'
	await_result screen OUTPUT_CLOSED=1
	await_result screen EXIT_STATUS=0
	"$topdir"/plainmouth action=dump id=screen filename="$current_dump"
	"$topdir"/plainmouth action=delete id=screen
	# The emulator's replies use POLLOUT without the user-input control filter.
	create_term reply 'stty -echo -icanon min 1 time 0; printf "\033[2;4H\033[6n"; reply=$(dd bs=1 count=6 2>/dev/null); test "$reply" = "$(printf "\033[2;4R")" || exit 4; printf "\033[Hreply ok"'
	await_result reply EXIT_STATUS=0
	await_text reply 'reply ok'
	"$topdir"/plainmouth action=delete id=reply
	# Drain more than one event's read budget even when HUP is also ready.
	create_term large "head -c 98304 /dev/zero | tr '\000' x; printf '\nFINAL\n'; exit 3"
	await_result large OUTPUT_CLOSED=1
	await_result large EXIT_STATUS=3
	await_text large FINAL
	"$topdir"/plainmouth action=delete id=large
	# Closing PTY output is independent of child exit; it must not send HUP.
	create_term early 'exec 0<&- 1>&- 2>&-; sleep 0.8; exit 9'
	await_result early OUTPUT_CLOSED=1
	grep -qx RUNNING=1 "$termdir/result"
	await_result early EXIT_STATUS=9
	"$topdir"/plainmouth action=delete id=early
	create_term first 'printf "first\n"; exit 11'
	create_term second 'printf "second\n"; exit 12'
	await_result first EXIT_STATUS=11
	await_result second EXIT_STATUS=12
	"$topdir"/plainmouth action=delete id=first
	"$topdir"/plainmouth action=delete id=second
	create_term input 'stty -echo; printf "READY\n"; IFS= read -r line; printf "received: %s\n" "$line"'
	await_text input READY
	local status=0
	"$topdir"/plainmouth action=set-value id=input input=$'discard\033' >/dev/null || status=$?
	test "$status" -eq 1
	status=0
	"$topdir"/plainmouth action=set-value id=input input=discard button=1 >/dev/null || status=$?
	test "$status" -eq 1
	"$topdir"/plainmouth action=set-value id=input input=$'hl0 \304\203\b\344\270\255\b\304\203X\b\n'
	await_result input EXIT_STATUS=0
	await_text input $'received: hl0 \304\203'
	grep -qx BUTTON_1=0 "$termdir/result"
	grep -qx INPUT_PENDING=0 "$termdir/result"
	status=0
	"$topdir"/plainmouth action=set-value id=input input=late >/dev/null || status=$?
	test "$status" -eq 1
	"$topdir"/plainmouth action=delete id=input
	# Cleanup escalates for a command ignoring HUP and TERM, without blocking UI.
	create_term live 'trap "" HUP TERM; printf "LIVE\n"; exec sleep 30'
	await_text live LIVE
	"$topdir"/plainmouth action=result id=live >"$termdir/result"
	pid=$(sed -n 's/^PID=//p' "$termdir/result")
	grep -qx RUNNING=1 "$termdir/result"
	"$topdir"/plainmouth action=delete id=live
	"$topdir"/plainmouth --ping >/dev/null
	await_reaped "$pid"
	local status=0
	create_term bad '' >/dev/null || status=$?
	test "$status" -eq 1
	status=0
	"$topdir"/plainmouth action=create plugin=termbox id=oversized command=true \
		width=65535 height=65535 >/dev/null || status=$?
	test "$status" -eq 1
	"$topdir"/plaindialog --stdout --termbox 'printf "client\n"; exit 5' 6 32 >"$termdir/client" &
	local client=$! id="plaindialog-$!" ready=false
	for ((n=0; n<100; n++)); do
		if "$topdir"/plainmouth action=result id="$id" >/dev/null 2>&1; then
			ready=true
			break
		fi
		sleep 0.01
	done
	test "$ready" = true
	await_result "$id" EXIT_STATUS=5
	"$topdir"/plainmouth action=set-value id="$id" button=1
	wait "$client"
	test ! -s "$termdir/client"
	# Server shutdown also terminates a still-running command.
	create_term shutdown 'printf "SHUTDOWN\n"; exec sleep 30'
	await_text shutdown SHUTDOWN
	"$topdir"/plainmouth action=result id=shutdown >"$termdir/result"
	sed -n 's/^PID=//p' "$termdir/result" >"$termdir/shutdown-pid"
}

testcase_view()
{
	trap '"$topdir"/plainmouth --quit >/dev/null 2>&1 || :' EXIT
	"$topdir"/plaindialog --termbox \
		'printf "\033[1;36mtermbox\033[0m\n"; exec bash --noprofile --norc -i' 10 60
}

exec 2>"$logfile"
run_test "testcase_$MODE" &
run_server
if [ "$MODE" = dump ]; then
	await_reaped "$(cat "$termdir/shutdown-pid")"
fi
verify_dump "$current_dump"
clear_testdata "$current_dump" "$probe" "$termdir/result" \
	"$termdir/client" "$termdir/shutdown-pid"
rmdir "$termdir"
