#!/bin/bash -efu
# SPDX-License-Identifier: GPL-2.0-or-later

progfile="$(readlink -f "$0")"
testsdir="${progfile%/*}"

. "$testsdir"/init-test

draw_testcase()
{
	"$topdir"/plainmouth plugin=msgbox action=create id=w1 \
		height=30 width=100 border=true \
		text="00 When I find my code in tons of trouble
01 Friends and colleagues come to me
02 Speaking words of wisdom
03 Write in C
04 
05 As the deadline fast approaches
06 And bugs are all I can see
07 Somewhere someone whispers
08 Write in C
09 
10 Write in C, write in C
11 Write in C, write in C
12 LISP is dead and buried
13 Write in C
14 
15 I used to write a lot of FORTRAN
16 For science it worked flawlessly
17 Try using it for graphics
18 Write in C
19 
20 If you just spent nearly 30 hours
21 Debugging some assembly
22 Soon you'll be glad to
23 Write in C
24 
25 Write in C, write in C
26 Write in C, yeah, write in C
27 Only wimps use BASIC
28 Write in C
29 
30 Write in C, write in C
31 Write in C, write in C
32 Pascal won't quite cut it
33 Write in C
34 
35 Write in C, write in C
36 Write in C, write in C
37 Don't even mention COBOL
38 Write in C
39 
40 And when the screen is fuzzing
41 And the editor is bugging me
42 I'm sick of ones and zeroes
43 Write in C
44 
45 A thousand people swear that
46 TP7 is the one for me
47 I hate the word \"procedure\"
48 Write in C
49 
50 Write in C, write in C
51 Write in C, write in C
52 PL/1 is '80s
53 Write in C
54 
55 Write in C, write in C
56 Write in C, write in C
57 The government loves Ada
58 Write in C
" \
	button="OK" \
	button="Cancel"
}

testcase_view()
{
	draw_testcase
	"$topdir"/plainmouth action=wait-result id=w1
	"$topdir"/plainmouth --quit
}

testcase_dump()
{
	draw_testcase
	"$topdir"/plainmouth action=dump id=w1 filename="$current_dump"
	"$topdir"/plainmouth --quit
}

exec 2>"$logfile"
run_test "testcase_${MODE:-dump}" &
run_server
verify_dump "$current_dump"
clear_testdata "$current_dump"
