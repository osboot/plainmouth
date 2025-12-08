#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=form action=create id=w1

#./plainmouth action=wait-result id=w1
#./plainmouth --quit
