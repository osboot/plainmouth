#!/bin/sh -efu

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=timebox action=create id=w1 width=16 height=4 border=true \
	button="OK" \
	button="Cancel" \
	#

./plainmouth action=wait-result id=w1

./plainmouth --quit
