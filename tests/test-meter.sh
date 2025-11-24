#!/bin/sh -efu

export PLAINMOUTH_SOCKET="${PLAINMOUTH_SOCKET:-/tmp/plainmouth.sock}"

./plainmouth plugin=meter action=create id=w1 total=100 percent=yes borders=yes

for i in 10 20 30 40 50 60 70 80; do
	./plainmouth action=update id=w1 value=$i
	sleep 0.3
done

for i in 90 100 110 120; do
	./plainmouth action=update id=w1 value=$i
	sleep 0.3
done

./plainmouth action=wait-result id=w1

./plainmouth action=delete id=w1
./plainmouth --quit
