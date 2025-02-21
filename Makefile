rsh:	main.c rsh.c rsh.h
	gcc main.c rsh.c -Wall -Og -g -o rsh

run:	rsh
	gdb ./rsh

all:	rsh run