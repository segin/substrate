# middle/passes

Optimization passes over legal SSA IR, run by `cc_run_middle_passes()`
(`opt.c`):

- `opt.c`: per function, constant folding and constant-branch folding,
  unreachable-code removal and dead-value elimination, and from -O1 the
  forwarding of a local slot stored once with a constant into its loads
  (`forward_const_slots()`), a compare's truth tested again (`!!x`,
  `!(a == b)`) the compare or its inverse (`fold_bool_not()`, integer
  compares only, through the extension that makes an int of it; and a
  value only ever a 0 or a 1 -- an asm's flag output, a bool a variable
  keeps (`truth_values()`) -- tested again or extended itself, again
  once legalization has extended it), and
  common subexpressions within a block
  (`cse_function()`: a pure computation made again from the same versions
  of the same operands a copy of the first, loads left alone as the IR
  does not mark the volatile ones); then, from -O1,
  inlining bottom-up over the call graph (a function is rewritten once
  its callees are, then folded again); at the end of a function's
  passes, the target's legalization once more, for what it split before
  the constants were known (on i386, a 64-bit division by a local that
  holds a power of two), after a parameter of a static function that
  every call of it passes the same constant has become that constant
  and the function been folded again (`constant_args()`, as gcc's
  interprocedural constant propagation); last, the static functions
  nothing refers to any more are dropped.
- `mem.c`: what is known of the function's own stack slots whose
  addresses do not escape (`cc_optimize_slots()`, run in the -O1 loop):
  a slot nothing loads from dropped with its stores and its zeroing
  memset, a slot stored once read as the value stored, a structure slot split
  into one slot per field (its zeroing memset into stores of 0), and a
  compare of a slot only stored constants folded where the set of them
  it can hold, followed over the blocks and narrowed by branches on it,
  decides it; and a scalar slot made a value (`cc_promote_slots()`),
  under an asm goto as anywhere, after which the definitions nothing
  reads go (`cc_dead_defs()`), liveness followed to an asm goto's
  labels too, and what that leaves one value folds again.
- `inline.c`: the inlining itself -- which functions may be inlined
  (`cc_inline_candidate()`: static and inline or always_inline, small,
  an interface of plain values or structures by value -- an argument's
  storage taken for the parameter where the body only reads it, a result
  stored to the call's buffer, its words or a copy from where it points
  -- no frame-dependent body; or, as gcc's
  `-finline-functions-called-once`, any static function called from one
  place, named nowhere else and on no cycle of calls, which `opt.c`
  marks, if it does not grow the caller's frame past gcc's limit) and
  the copy of a body in place of a call (`cc_inline_calls()`), which
  hands the caller the callee's `keep_syms` too: the static locals
  marked `used` that the object keeps while some function still holds
  them, whether or not its code names them.

Not here yet: mem2reg, CSE across blocks (GVN), LICM and loop transforms.
