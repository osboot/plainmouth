#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth \
		plugin=form action=create id=w1 width=30 height=7 border=true \
		hbox=start label="Username:" input="legion" hbox=end \
		hbox=start label="Password:" password="" hbox=end \
		hbox=start label="Extra1:"   input="extra 1" hbox=end \
		hbox=start label="Extra2:"   input="extra 2" hbox=end \
		hbox=start label="Extra3:"   input="extra 3" hbox=end \
		hbox=start label="Extra4:"   input="extra 4" hbox=end \
		hbox=start label="Extra5:"   input="extra 5" hbox=end \
		hbox=start label="Extra6:"   input="extra 6" hbox=end \
		\
		button="OK" button="Cancel" \
	#
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_${MODE:-dump}" &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump"
