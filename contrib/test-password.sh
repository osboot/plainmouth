#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=password action=create id=w1 x=1 y=1 \
	text="(1) Enter password:
123456789 123456789 123456789" \
	label="qwerty
asdfghjk: "

./plainmouth plugin=password action=create id=w2 x=20 y=4 text="(2) Enter password:"
./plainmouth plugin=password action=create id=w3 x=36 y=6

./plainmouth action=wait-result id=w1
./plainmouth action=wait-result id=w2
./plainmouth action=wait-result id=w3

./plainmouth --quit
