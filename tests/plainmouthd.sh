#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

valgrind_prog="$(type -p valgrind)"

run()
{
	[ -z "$valgrind_prog" ] ||
		exec valgrind --leak-check=full --show-leak-kinds=all --log-file=/tmp/valgrind.log "$@"
	exec "$@"
}

run ./plainmouthd -S "$PLAINMOUTH_SOCKET" --debug-file=/tmp/server.log
