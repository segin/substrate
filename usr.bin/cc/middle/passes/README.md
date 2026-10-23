# middle/passes

Optimization passes over legal SSA IR, run by `cc_run_middle_passes()`
(`opt.c`):

- `opt.c`: per function, constant folding and constant-branch folding,
  unreachable-code removal and dead-value elimination, and from -O1 the
  forwarding of a local slot stored once with a constant into its loads
  (`forward_const_slots()`); then, from -O1,
  inlining bottom-up over the call graph (a function is rewritten once
  its callees are, then folded again); last, the static functions
  nothing refers to any more are dropped.
- `mem.c`: what is known of the function's own stack slots whose
  addresses do not escape (`cc_optimize_slots()`, run in the -O1 loop):
  a slot stored once read as the value stored, a structure slot split
  into one slot per field (its zeroing memset into stores of 0), and a
  compare of a slot only stored constants folded where the set of them
  it can hold, followed over the blocks and narrowed by branches on it,
  decides it; and a scalar slot made a value (`cc_promote_slots()`),
  under an asm goto as anywhere.
- `inline.c`: the inlining itself -- which functions may be inlined
  (`cc_inline_candidate()`: static and inline or always_inline, small,
  plain-value interface, no frame-dependent body; or, as gcc's
  `-finline-functions-called-once`, any static function called from one
  place, named nowhere else and on no cycle of calls, which `opt.c`
  marks, if it does not grow the caller's frame past gcc's limit) and
  the copy of a body in place of a call (`cc_inline_calls()`).

Not here yet: mem2reg, GVN/CSE, LICM and loop transforms.
