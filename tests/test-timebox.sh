#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=timebox action=create id=w1 width=16 height=4 \
	button="OK" \
	button="Cancel" \
	#

./plainmouth action=wait-result id=w1

./plainmouth --quit
