#!/bin/sh -efu

progname="$(readlink -f "$0")"
testsdir="${progname%/*}"
topdir="${testsdir%/*}"

export LD_LIBRARY_PATH="$topdir"

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"
export PLAINMOUTH_PLUGINSDIR="$topdir/plugins"

valgrind_prog="$(type -p valgrind)"

run()
{
	[ -z "$valgrind_prog" ] ||
		exec valgrind --leak-check=full --show-leak-kinds=all \
			--suppressions=./tests/valgrind.supp \
			--log-file=/tmp/valgrind.log "$@"
	exec "$@"
}

run ./plainmouthd -S "$PLAINMOUTH_SOCKET" --debug-file=/tmp/server.log
