#!/bin/sh -efu

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=password action=create id=w1 x=1 y=1 width=40 height=5 border=true \
	text="(1) Enter password:
123456789 123456789 123456789" \
	label="qwerty asdfghjk: "

./plainmouth plugin=password action=create id=w2 x=20 y=4 width=30 height=4 border=true text="(2) Enter password:"
./plainmouth plugin=password action=create id=w3 width=20 height=3 x=36 y=6 border=true

./plainmouth action=wait-result id=w1
./plainmouth action=wait-result id=w2
./plainmouth action=wait-result id=w3

./plainmouth --quit
