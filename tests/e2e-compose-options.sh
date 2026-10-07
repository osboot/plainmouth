#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"
. "$testsdir"/init-test

expect_value()
{
	test "$("$topdir"/plainmouth action=get-value id=filter node-id=results)" = "VALUE=$1"
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

expect_unchanged()
{
	rm -f -- "$current_dump.after"
	"$topdir"/plainmouth action=dump id=filter filename="$current_dump.after"
	cmp "$current_dump" "$current_dump.after"
}

testcase()
{
	local items=(Alpha Alpine Beta Bravo Delta Echo Foxtrot Gamma Hotel India Juliet Kilo Lima Mike November Omega)
	local -a options=()
	local item i

	for item in "${items[@]}"; do
		options+=("option=$item")
	done

	"$topdir"/plainmouth action=create plugin=compose id=filter width=32 height=12 border=true \
		node=vbox \
		  node=input node-id=query value= notify=true node=end \
		  node=select node-id=results visible=5 notify=true "${options[@]}" node=end \
		  node=button node-id=ok text=OK node=end \
		node=end

	if [ "$MODE" = view ]; then
		local event query

		while event=$("$topdir"/plainmouth action=wait-event id=filter); do
			case "$event" in
				*NODE_ID=query*) ;;
				*) continue ;;
			esac

			query=$("$topdir"/plainmouth action=get-value id=filter node-id=query)
			query=${query#VALUE=}
			options=()

			for item in "${items[@]}"; do
				if [[ ${item,,} == *"${query,,}"* ]]; then
					options+=("option=$item")
				fi
			done

			if [ "${#options[@]}" -eq 0 ]; then
				options=(clear=true)
			fi

			"$topdir"/plainmouth action=update id=filter node-id=results "${options[@]}"
		done

		"$topdir"/plainmouth action=wait-result id=filter
		"$topdir"/plainmouth --quit
		return
	fi

	"$topdir"/plainmouth action=set-value id=filter node-id=results value=16
	"$topdir"/plainmouth action=update id=filter node-id=results disabled=true
	expect_value 16
	"$topdir"/plainmouth action=update id=filter node-id=results disabled=false option=One option=Two value=2
	expect_value 2
	"$topdir"/plainmouth action=dump id=filter filename="$current_dump"
	local -a invalid=(
		'option=Bad clear=true' 'clear=true value=0' 'clear=maybe' 'clear=true clear=true'
		'option=Bad value=2' 'option=Bad value=0' 'option=Bad value=bad'
		'option=Bad value=1 value=1' 'value=1' 'clear=false'
		'option=Bad disabled=maybe' 'option=Bad readonly=maybe' 'option=Bad unknown=true'
	)
	local args
	local -a invalid_args

	for args in "${invalid[@]}"; do
		read -r -a invalid_args <<< "$args"
		expect_error action=update id=filter node-id=results "${invalid_args[@]}"
		expect_unchanged
		expect_value 2
	done

	expect_error action=update id=filter node-id=query option=Bad
	expect_error action=update id=filter node-id=query clear=true
	expect_error action=update id=filter node-id=results disabled=true \
		'option=This option is much too wide for this window'
	expect_unchanged
	local oversized
	printf -v oversized '%4097s' ''
	expect_error action=update id=filter node-id=results "option=$oversized"
	expect_unchanged
	options=()

	for ((i=0; i<257; i++)); do
		options+=(option=TooMany)
	done

	expect_error action=update id=filter node-id=results "${options[@]}"
	expect_unchanged
	"$topdir"/plainmouth action=update id=filter node-id=results option=Replacement
	expect_value 1
	"$topdir"/plainmouth action=update id=filter node-id=results clear=true
	"$topdir"/plainmouth action=update id=filter node-id=results clear=true
	expect_value 0
	expect_error action=set-value id=filter node-id=results value=1
	"$topdir"/plainmouth action=update id=filter node-id=results option=Restored option=Final value=2
	expect_value 2
	"$topdir"/plainmouth action=update id=filter node-id=results clear=true
	"$topdir"/plainmouth action=set-value id=filter node-id=ok clicked=true
	# Programmatic replacement and clearing must not enqueue change events.
	expect_error action=wait-event id=filter
	test "$("$topdir"/plainmouth action=wait-result id=filter)" = \
		"$(printf 'INPUT_2=\nSELECT_3=0\nBUTTON_4=1')"
	"$topdir"/plainmouth action=delete id=filter
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test testcase &
test_pid=$!
run_server "$test_pid"
clear_testdata "$current_dump" "$current_dump.after"
