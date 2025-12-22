#!/bin/sh -efu

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=password action=create id=w1 x=20 y=4 width=30 height=3 border=true \
	label="Enter password:" \
	tooltip="Passwords must be at least 10 characters in length
a minimum of 1 lower case letter [a-z]
a minimum of 1 upper case letter [A-Z]
a minimum of 1 numeric character [0-9]
a minimum of 1 special character: ~\`!@#$%^&*()-_+={}[]|\;:\"<>,./?"

./plainmouth action=wait-result id=w1

./plainmouth --quit
