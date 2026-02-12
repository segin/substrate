# Makefile for host-side testing of libuu

all: test

test: test_libuu
	./test_libuu

test_libuu: test_libuu.c ../uudecode.c
	cc -g -Wall -I.. -o $@ $^

clean:
	rm -f test_libuu
