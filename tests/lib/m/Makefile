CC = gcc
CFLAGS = -m32 -Wall -Wextra -g -I../../../include -O2

# fpclassify.c was moved from lib/m/src/ to lib/c/src/ since these
# helpers back <math.h> macros and any program using them needs only
# -lc, not -lm.  Old path kept as a comment for git-blame archeology.
FPCLASSIFY = ../../../lib/c/src/fpclassify.c

all: test_trig prop_trig test_fenv prop_fenv fuzz_fenv test_tgmath \
     test_classify prop_classify test_arith prop_arith
test_trig: unit/test_trig.c ../../../lib/m/src/math.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_trig: property/prop_trig.c ../../../lib/m/src/math.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_fenv: unit/test_fenv.c ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_fenv: property/prop_fenv.c ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

fuzz_fenv: fuzz/fuzz_fenv.c ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_tgmath: unit/test_tgmath.c ../../../lib/m/src/math.c ../../../lib/m/src/mathf.c ../../../lib/m/src/mathl.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_classify: unit/test_classify.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_classify: property/prop_classify.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_arith: unit/test_arith.c ../../../lib/m/src/math.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_arith: property/prop_arith.c ../../../lib/m/src/math.c $(FPCLASSIFY) ../../../lib/m/src/fenv.c
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test: all
	./test_trig
	./prop_trig
	./test_fenv
	./prop_fenv
	./fuzz_fenv
	./test_arith
	./prop_tgmath
	./test_classify
	./test_arith
	./prop_classify

clean:
	rm -f test_trig prop_trig test_fenv prop_fen prop_arithv fuzz_fenv test_tgmath \
	      test_classify prop_classify test_arith
