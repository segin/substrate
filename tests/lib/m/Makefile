CC = gcc
CFLAGS = -m32 -Wall -Wextra -g -I../../../include -O2

# fpclassify.c was moved from lib/m/src/ to lib/c/src/ since these
# helpers back <math.h> macros and any program using them needs only
# -lc, not -lm.  Old path kept as a comment for git-blame archeology.
FPCLASSIFY = ../../../lib/c/src/fpclassify.c

# lib/m/src/math.c was split into per-family files; tests link the
# same set the real library does.
MATH_SRCS = ../../../lib/m/src/math_exp.c \
            ../../../lib/m/src/math_trig.c \
            ../../../lib/m/src/math_special.c \
            ../../../lib/m/src/math_round.c \
            ../../../lib/m/src/math_wrap.c
FENV = ../../../lib/m/src/fenv.c

all: test_trig prop_trig test_fenv prop_fenv fuzz_fenv test_tgmath \
     test_classify prop_classify test_arith prop_arith test_round prop_round \
     test_explog prop_explog test_bessel prop_bessel \
     test_accuracy test_x87_opt fuzz_math fuzz_mathf \
     test_libm_new prop_totalorder

test_trig: unit/test_trig.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_trig: property/prop_trig.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_fenv: unit/test_fenv.c $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_fenv: property/prop_fenv.c $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

fuzz_fenv: fuzz/fuzz_fenv.c $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_tgmath: unit/test_tgmath.c $(MATH_SRCS) ../../../lib/m/src/mathf.c ../../../lib/m/src/mathl.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_classify: unit/test_classify.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_classify: property/prop_classify.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_arith: unit/test_arith.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_arith: property/prop_arith.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_round: unit/test_round.c $(MATH_SRCS) ../../../lib/m/src/mathl.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_round: property/prop_round.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_explog: unit/test_explog.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_explog: property/prop_explog.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_bessel: unit/test_bessel.c $(MATH_SRCS) ../../../lib/m/src/mathf.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_bessel: property/prop_bessel.c $(MATH_SRCS) $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_accuracy: unit/test_accuracy.c $(MATH_SRCS) ../../../lib/m/src/mathf.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_x87_opt: unit/test_x87_opt.c $(MATH_SRCS) ../../../lib/m/src/mathf.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

fuzz_math: fuzz/fuzz_math.c $(MATH_SRCS) ../../../lib/m/src/mathf.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

fuzz_mathf: fuzz/fuzz_mathf.c $(MATH_SRCS) ../../../lib/m/src/mathf.c ../../../lib/m/src/mathl.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test_libm_new: unit/test_libm_new.c $(MATH_SRCS) ../../../lib/m/src/math_obsolete.c ../../../lib/m/src/math_narrowing.c ../../../lib/m/src/math_totalorder.c ../../../lib/m/src/math_reentrant.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

prop_totalorder: property/prop_totalorder.c $(MATH_SRCS) ../../../lib/m/src/math_totalorder.c $(FPCLASSIFY) $(FENV)
	$(CC) $(CFLAGS) -fno-builtin $^ -o $@ -lm

test: all
	./test_trig
	./prop_trig
	./test_fenv
	./prop_fenv
	./fuzz_fenv
	./test_arith
	./prop_arith
	./test_classify
	./prop_classify
	./test_round
	./prop_round
	./test_explog
	./prop_explog
	./test_bessel
	./prop_bessel
	./test_accuracy
	./test_x87_opt
	./fuzz_math
	./fuzz_mathf
	./test_libm_new
	./prop_totalorder

clean:
	rm -f test_trig prop_trig test_fenv prop_fenv fuzz_fenv test_tgmath \
	      test_classify prop_classify test_arith prop_arith \
	      test_round prop_round test_explog prop_explog \
	      test_bessel prop_bessel \
	      test_accuracy test_x87_opt fuzz_math fuzz_mathf \
	      test_libm_new prop_totalorder
