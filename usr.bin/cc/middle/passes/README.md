# middle/passes

Optimization passes over legal SSA IR, run by `cc_run_middle_passes()`
(`opt.c`):

- `opt.c`: per function, constant folding and constant-branch folding,
  unreachable-code removal and dead-value elimination; then, from -O1,
  inlining bottom-up over the call graph (a function is rewritten once
  its callees are, then folded again); last, the static functions
  nothing refers to any more are dropped.
- `inline.c`: the inlining itself -- which functions may be inlined
  (`cc_inline_candidate()`: static and inline or always_inline, small,
  plain-value interface, no frame-dependent body) and the copy of a body
  in place of a call (`cc_inline_calls()`).

Not here yet: mem2reg, GVN/CSE, LICM and loop transforms.
