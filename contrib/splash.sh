#!/bin/ash -efu

./plainmouth /tmp/sock plugin=splash action=create total=120 percent=1 borders=0 focus=1 >/tmp/x

w=$(sed -n -e 's/^WIDGET=//p' /tmp/x)
[ -n "$w" ]

for i in 10 20 30 40 50 60 70 80; do
	./plainmouth /tmp/sock plugin=splash action=update widget=$w value=$i
	sleep 1
done

./plainmouth /tmp/sock --hide-splash
sleep 3
./plainmouth /tmp/sock --show-splash

for i in 90 100 120 140; do
	./plainmouth /tmp/sock plugin=splash action=update widget=$w value=$i
	sleep 1
done

./plainmouth /tmp/sock plugin=splash action=delete widget=$w
./plainmouth /tmp/sock --quit
