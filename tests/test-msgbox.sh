#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=msgbox action=create id=w1 height=7 \
	text="ВАЖНО!
тут какое-то важное сообщение." \
	button="OK" \
	button="Cancel" \
	button="второе" \
	button="ещё что-то"

./plainmouth action=wait-result id=w1

./plainmouth action=delete id=w1
./plainmouth --quit
