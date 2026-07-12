# usr.lib/bc audit - 2026-07

Full read of `usr.lib/bc/` (num.h, num.c 413 lines, arith.c 841 lines,
Makefile) at commit 130aae793, with behavioral checks against a freshly
built host binary.  This is the arbitrary-precision (base-100) number
engine behind `bin/bc`.  Findings numbered LBC-NN.

Status: **all 6 findings fixed** (LBC-01..LBC-06), verified against a
freshly built host binary; host and substrate target builds clean under
-Werror.  See `git log` for the per-finding commits.

What's solid and verified correct: Knuth-D division and modulo (the
detailed comments document real past fixes; sqrt(2)@20, mod signs, and
non-integer-scale remainders all check out), sqrt via Newton with guard
digits, the -l transcendental series (e/l/a/s/c/j) with argument
reduction and growing work-scale, POSIX multiply/add scale rules,
negative-zero normalization, and the base-100 add/sub carry/borrow.
The bugs below are truncation, leaks, and overflow at the edges - the
core arithmetic is sound.

## High

### LBC-01: bc_print_base silently truncates integer output at 1024 digits
`out_digits` is a fixed `malloc(1024 * sizeof(int))` (num.c:312-313)
and the collection loop guards every push with `if (ds < max_digits)`
(num.c:319,327), so any value needing more than 1024 output digits in a
non-decimal obase is **silently truncated** - a wrong answer with no
error.  Verified: `obase=2; 2^1200` prints 1024 binary digits instead
of 1201.  (Decimal output goes through bc_print, which streams from the
base-100 array and is unaffected - `2^5000` prints all 1506 digits.)
Fix: size out_digits from the value's magnitude (it is known:
`int_part->len*2 * log2(100)/log2(obase)` bounds it) or grow it
dynamically.

## Medium

### LBC-02: per-call memory leaks in the -l math functions
Several `bc_div`/`bc_mul` calls take an inline `bc_from_long(...)`
temporary as an argument.  bc_div/bc_mul deep-copy their operands and
never free them, so each inline temporary leaks:
- num arith.c:677 `bc_div(one, bc_from_long(3))`
- arith.c:682 `bc_div(bc_from_long(3), two)`
- arith.c:683 `bc_div(bc_from_long(2), bc_from_long(3))` (two leaks)
- arith.c:708 `bc_div(one, bc_from_long(10))`
- arith.c:756 `bc_div(bc_from_long(1), bc_from_long(2))`
Every `l()`, `a()`, `s()`, `c()` (via reduce_angle) call leaks a
handful of bc_nums.  Fix: bind each temporary to a local and bc_free it
(or add freeing div/mul helpers).

### LBC-03: bc_num_to_long has no overflow guard
`res += (long long)val * p; ... p *= 100;` (num.c:114-122) overflows
`p` and `res` (signed - UB) for any value beyond ~19 digits.  It feeds
array-index conversion (capped by BC_DIM_MAX, safe), but also the
ibase/obase/scale setters and `j(n,x)`'s order, where a large magnitude
wraps to a garbage int.  Fix: clamp to [LLONG_MIN, LLONG_MAX] and stop
accumulating once the integer part is exhausted.

## Low

### LBC-04: ibase/obase/scale truncate assignments above INT_MAX
`bc_ibase`/`bc_obase`/`bc_scale` are `int` (num.c:13-15).  Assigning a
value larger than INT_MAX (via LBC-03's unclamped conversion) wraps.
Verified: `obase=1099511627776` (2^40, which wraps to 0, then clamps to
2) makes `5` print as `101`.  Fix: clamp the conversion and reject
out-of-range bases with an error (GNU caps obase and errors on ibase).

### LBC-05: unchecked malloc in bc_print_base
`out_digits = malloc(...)` (num.c:313) is dereferenced without a NULL
check, unlike bc_new/bc_expsize which perror+exit.  A failed allocation
segfaults.  Fix: check (and match the library's exit-on-OOM
convention).

### LBC-06: dead code / avoidable recompute
- `bc_sqrt` allocates `one` (arith.c:541) and never uses it (only
  `two` is used in the iteration); it is freed, so no leak, just dead.
- `compute_pi()` recomputes 4*atan(1) at full working scale on **every**
  `s()`/`c()` call (bc_sincos, arith.c:770) - correct but wasteful; a
  cached pi at the requested scale would help trig-heavy scripts.
- `bc_align_scale` is declared to return `bc_num *` but always returns
  NULL (arith.c:54) and every caller ignores it; make it void.

## Verification method
Probed against a freshly linked host binary (num.o + arith.o):
L1 obase=2 2^1200 (1024 vs 1201 digits), L2 scale=3 2.5^3=15.625,
L4 obase=2^40, L5 scale=20 sqrt(2), L6 -7%3=-1, L7 0^0=1, L8 2^5000
full decimal.  One suspected scale bug ((1/3)^2 at scale 5 giving
.11110) was probed and is CORRECT per POSIX pow scale rules
(min(scale*exp, max(scale,scale)) = 5; .33333^2 truncates to .11110) -
not a finding.
