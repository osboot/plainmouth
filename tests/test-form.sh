#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=form action=create id=w1 width=30 height=5 border=true \
	hbox=start label="Username:" input="legion" hbox=end \
	hbox=start label="Password:" password="" hbox=end \
	\
	button="OK" button="Cancel" \
	#

./plainmouth action=wait-result id=w1
./plainmouth --quit
