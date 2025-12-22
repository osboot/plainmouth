#!/bin/sh -efu

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=checklist action=create id=w1 width=40 height=7 border=true \
	select=1 visible=5 \
	option="apple" \
	option="banana" \
	option="orange" \
	option="mango" \
	option="pineapple" \
	option="grapes" \
	option="raspberry" \
	button="OK" \
	button="Cancel" \
	#

./plainmouth action=wait-result id=w1

./plainmouth --quit
