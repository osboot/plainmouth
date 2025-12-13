#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth action=set-title message="Unknown linux"

./plainmouth plugin=msgbox action=create id=w1 width=40 height=7 \
	text="ВАЖНО!
тут какое-то важное сообщение." \
	button="OK" \
	button="Cancel" \
	button="второе" \
	button="ещё что-то"

./plainmouth action=wait-result id=w1

./plainmouth action=delete id=w1
./plainmouth --quit
