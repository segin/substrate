# bin/bc audit - 2026-07

Full read of `bin/bc/` (bc.c, 1,778 lines + Makefile) at commit
1199d6051, plus behavioral verification of every suspected defect
against a freshly built host binary (the checked-in `bc_host` was
stale; see BC-20).  The libbc number engine (`usr.lib/bc/`) is out of
scope except where its behavior surfaces through the interpreter.
Findings numbered BC-NN, ranked by severity.  Status: **all open**
(audit only - no fixes applied).

What's solid: the AST ownership discipline (tok_num/tok_str handover,
ast_free covering every node shape), the BC_DIM_MAX cap with its
overflow rationale, by-ref array copy-in/copy-out robustness against
callee reallocation, argument binding evaluated in the caller's scope
before the frame push, the banner-to-stderr-only-on-tty behavior, and
`&&`/`||` matching GNU's no-short-circuit semantics.

## High

### BC-01: a stray break/continue/return at top level silently kills the rest of the session
`eval_stmt` sets `is_breaking` / `is_continuing` / `is_returning` and
returns; nothing at top level ever clears them, and the guard at
eval_stmt entry (bc.c:1574) then skips every subsequent statement.
Verified: `break` followed by `1+1` prints nothing - the interpreter
keeps parsing but executes nothing for the remainder of the script or
interactive session.  Fix: report "break outside loop" etc. (POSIX
requires it) or at minimum clear the flags after each top-level
statement in the three main loops.

### BC-02: every error is a hard exit(1) - a single typo kills the interactive session
`lex_error`, `match`, and all runtime failures (undefined function,
array-where-scalar) call exit(1).  Verified interactively: `1+@`
prints one syntax error and the process exits - the user loses their
whole session state.  Also: runtime errors are mislabeled
"syntax error at line N" where N is the LEXER's current position, not
the offending statement (a function called at line 500 whose body
errors reports the line the lexer last touched).  GNU bc recovers and
keeps the session alive.  Fix: longjmp back to the REPL/statement loop
on error; separate runtime-error reporting from lex_error.

### BC-03: multi-line constructs are impossible at the interactive prompt
The REPL (bc.c:1743-1764) feeds each getline() line to the parser as a
complete program via lex_set_string.  A construct that continues past
the line - `define f(x) {`, `while (x) {`, an open parenthesis - hits
TOK_EOF mid-parse, raises "expected expression primary", and (per
BC-02) exits the process.  Verified: typing a 3-line function
definition kills bc at line 2.  Fix: keep a persistent lexer across
lines (feed the FILE-based lexer from the terminal, or accumulate
until the parse completes), which also makes BC-02's recovery point
natural.

### BC-04: compound assignment to an array element evaluates the index twice
`a[i] += v` evaluates the subscript once for the read
(`eval_expr(lval)`, bc.c:1320) and again for the store (bc.c:1332):
side effects double-apply and the read and write can hit different
elements.  Verified: `i=0; a[0]=0; a[i++] += 5` leaves i=2 (not 1),
a[0]=0 unchanged, and the 5 stored in a[1].  Fix: evaluate the index
once and reuse it for read + store.

### BC-05: ++ and -- on array elements silently do nothing
The inc/dec evaluator only stores back for `AST_VAR`
(bc.c:1350: `if (n->unop.expr->type == AST_VAR) set_var_val(...)`);
an `AST_ARRAY` operand is read, incremented, returned - and never
written back.  Verified: `a[0]=5; a[0]++; a[0]` prints 5.  Fix: add
the array store path (sharing BC-04's evaluate-index-once helper).

## Medium

### BC-06: per-operation memory leaks in compound assignment and ++/--
Two leaks, both in loops' hot paths:
- Compound assign (bc.c:1338): `set_var_val`/`set_arr_val` duplicate
  `res` internally, then the case returns `bc_dup(res)` - `res` itself
  is never freed.  One bc_num leaked per `x += 1`.
- Inc/dec (bc.c:1350): `set_var_val(id, bc_dup(res))` - set_var_val
  dups its argument again, and the extra dup is never freed.  One
  bc_num leaked per `i++`.
`while (i < 10^6) i++` leaks a million numbers.  Fix: return `res`
directly (transfer ownership) and drop the extra dup.

### BC-07: missing call arguments silently alias same-named globals
Parameter binding walks `while (fp && ap)` (bc.c:1438): surplus
arguments are dropped and unbound parameters simply don't get a local,
so the function body's lookups fall through to the GLOBAL of the same
name.  Verified: `define f(a,b){return a+b}; b=99; f(1)` returns 100.
POSIX requires the argument count to match.  Fix: error on mismatch
(or bind missing params to fresh zero locals at minimum).

### BC-08: standard input is not read after file operands
POSIX bc: "After all files have been read, bc shall read the standard
input."  main() returns immediately after the file loop (bc.c:1740).
Verified: `printf '1+1\n' | bc /dev/null` prints nothing.  Fix: fall
through to the stdin path after processing files (this is also how
`bc -l prelude.bc` pipelines are used in practice).

### BC-09: read() is reserved but unimplemented - any use is a fatal parse error
The lexer returns TOK_READ (bc.c:452) but parse_primary has no case
for it, so `x = read()` dies with "expected expression primary" (and
per BC-02 kills the script).  The keyword also shadows `read` as an
identifier.  Fix: implement it (read a line from stdin, parse in
current ibase) or un-reserve the word.

### BC-10: unbounded recursion crashes the process
No call-depth or parse-depth limit: `define f() { return f() }; f()`
SIGSEGVs (verified, rc=139 with an 8 MiB stack), and deeply nested
parentheses drive the recursive-descent parser off the C stack the
same way.  Fix: a depth counter in eval_expr's AST_CALL (GNU-style
"call stack too deep" error) and one in parse_primary.

### BC-11: numeric literals, names, and strings silently truncated
Fixed lexer buffers drop overflow characters: numbers >1023 digits
(bc.c:395), names >255 (bc.c:423), strings >1023 (bc.c:467).
Verified: a 2000-digit literal quietly computes as its first 1023
digits (length() = 1023) - silently wrong arithmetic.  bc is
arbitrary-precision; the token buffers should grow (or at least
error).

## Low

### BC-12: no newline after values printed in obase > 16
Auto-print gates the trailing newline on `bc_obase <= 16` (bc.c:1689)
and bc_print_base emits none itself.  Verified with obase=17: all
values run together on a single unterminated line.

### BC-13: division by zero auto-prints a spurious 0
`1/0` prints libbc's "runtime error: divide by zero" but the
expression statement still auto-prints the returned zero value.  GNU
prints only the error.  Cosmetic-plus (scripts capturing stdout see a
phantom 0).

### BC-14: unchecked allocations
get_global's calloc/strdup (bc.c:248-249), register_func's
malloc/strdup (bc.c:265-266), the parser's expr_list callocs, and all
four array-grow paths (bc.c:1130/1149/1213/1232 - where a failed
realloc overwrites the array pointer with NULL: leak plus NULL deref
in the following memset).  Inconsistent with ast_new, which checks.

### BC-15: `for (;;)` and empty for-clauses are a fatal error
parse_expr is called unconditionally for init/cond/inc (bc.c:855-859);
an empty clause hits "expected expression primary" and exits.  GNU
treats an omitted condition as true.

### BC-16: ibase/obase/scale cannot be function autos
get_var_val/set_var_val special-case the three names before the local
lookup (bc.c:1104,1171), so `auto ibase` in a function still reads and
writes the global setting.  POSIX permits them as autos.

### BC-17: quit is executed at runtime, not lex time
`quit` maps to TOK_HALT (bc.c:445, deliberate per its comment), so
`if (0) quit` does not exit - GNU/POSIX quit terminates as soon as it
is lexed, even in unexecuted code.  Verified.  Acceptable as a
documented deviation; noted for conformance work.

### BC-18: '*' by-ref marker accepted on scalar parameters
`define f(*x)` parses; the binder strips the '*' and silently passes
by value (byref list is only built for array params, bc.c:1459).
Should be a parse error ("* requires an array parameter").

### BC-19: dead code and diagnostics polish
The '/' two-char-operator branch (bc.c:500) is unreachable - '/' is
intercepted by the comment scanner, which already handles /= (its own
comment says so).  `isalpha(c) && islower(c)` (bc.c:416) is redundant.
match() reports raw token numbers ("expected token 40, got 10") -
unreadable for users.

### BC-20: NATIVE_BUILD of bin/bc cannot link - host test harness broken
bin/bc/Makefile hardcodes `LDADD = -l:libbc.so.0`; under NATIVE_BUILD=1
the host link finds only the i386 substrate .so ("skipping
incompatible ... cannot find -l:libbc.so.0") and fails, so
`tests/bin/bc make build` (which copies the result to bc_host) has been
failing - the checked-in bc_host predated bc.c by three weeks (Jun 3
vs Jun 28).  Fix: link libbc.a (or $(BC_LIB)) for NATIVE_BUILD; the
static archive is already a declared prerequisite.

## Verification method
Suspicions were probed against a freshly linked host binary
(bc.host.o + usr.lib/bc native objects): P1 array-++ no-op, P2/P3
break/return wedge, P4 param aliasing, P5 stdin-after-files, P8
read(), P9 for(;;), P10 obase>16 newline, P12 interactive multi-line /
typo exit (under script(1) for a real tty), P13 recursion SIGSEGV,
P15/P19 truncation, P16 quit semantics, P17 double index eval.
Negative-zero handling was suspected and probed - libbc handles it
correctly (prints 0, compares equal), so it is NOT a finding.
