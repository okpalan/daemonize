
all: src/daemonize.c
	gcc -o daemonize src/daemonize.c  -Iinclude