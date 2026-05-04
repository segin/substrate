CC = gcc
CFLAGS = -m32 -Wall -Wextra -g -I../include -O2

all: test_trig prop_trig fuzz_fenv

test_trig: unit/test_trig.c ../../../lib/m/src/math.c ../../../lib/m/src/fpclassify.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_trig: property/prop_trig.c ../../../lib/m/src/math.c ../../../lib/m/src/fpclassify.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

fuzz_fenv: fuzz/fuzz_fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test: all
	./test_trig
	./prop_trig
	./fuzz_fenv

clean:
	rm -f test_trig prop_trig fuzz_fenv
