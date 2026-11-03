# middle/passes

Optimization passes over legal SSA IR, run by `cc_run_middle_passes()`
(`opt.c`):

- `opt.c`: per function, constant folding and constant-branch folding
  (a value every definition of which copies one constant -- across a
  change of size when the constant means the same at every width -- is
  that constant -- and, on i386, a pair masked with 32 bits of ones its
  low word, as 64-bit arithmetic cut to a long is; after the last
  passes, folding repeats while it drops
  arms that cannot run or folds anything, `settle_function()`), jump threading
  (`thread_jumps()`: a way into a block that only tests a variable goes
  where the value it sets goes, or branches on the compare it sets, a
  jump to the next label, its only way in, merging the blocks first; a
  branch both of whose ways go to one place, or to two returns of one
  value, a jump),
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
  of the same operands a copy of the first, an operand read as what the
  block copied it from while neither is written; it runs again once
  slots are values), and common loads across blocks (`cse_loads()`: a
  load lowering marked plain -- of an lvalue neither volatile nor
  atomic, `cc_ssa_instr_t.plain`; never another; not of a scalar local
  `mem.c` makes a value -- of an address, a base
  and a constant offset, that a plain load read in a block dominating it
  along blocks each entered from one place, a copy of what that read,
  unless something between may write it: a store but one to another
  stack slot or, from a slot, to a global, a call, a load not plain, an
  asm that clobbers "memory" or has a memory output, the write of a value
  whose address is taken; as gcc's value numbering, so a branch on the
  first read tells range folding of the second -- Linux's
  fuse_notify_store() reads `outarg.offset` twice); then, from -O1,
  inlining bottom-up over the call graph (a function is rewritten once
  its callees are, then folded again, with what the module's
  declarations tell, `fold_module_facts()`: a load from a const table at
  a constant offset its initializer's bytes, and so a load from a stack
  slot one memcpy fills from such a table and nothing else writes or
  lets escape (`fold_const_copies()`: Linux's pack_fields() checks a
  copy of each element of its field table), a load from a string
  literal at a constant offset the literal's byte (`"..."[k]`, as
  iwlwifi's printk wrappers check their format's last character), and
  a compare of two
  constant offsets from one object's address the compare of the
  offsets, the address of a stack slot or of an object or a function
  the module defines, not weak, not null; and a loop computing only from
  constants and const tables, entered once, run to its end and replaced
  by the constants it leaves and a jump out, `eval_const_loops()`, as
  gcc unrolls and folds it); at the end of a function's
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
  memset, a slot a memcpy fills whole from another slot zeroed and
  stored into for nothing but that copy made that slot itself
  (`coalesce_copied_slots()`: a structure an inlined function returns
  by value, copied out to the local that takes it), a slot a memcpy
  fills whole and that is only read after it, in
  the same straight run with nothing between that may write the source,
  read from the source (`forward_memcpy_slots()`: an inlined function's
  structure argument), a slot stored once read as the value stored, a structure slot split
  into one slot per field (its zeroing memset into stores of 0), and a
  compare of a slot only stored constants folded where the set of them
  it can hold, followed over the blocks and narrowed by branches on it,
  decides it; and a scalar slot made a value (`cc_promote_slots()`: a
  slot of a word or less, a one-word structure's among them, a whole
  zeroing memset of it the value 0 and a whole memcpy to or from it a
  copy, a load or a store),
  under an asm goto as anywhere, after which the definitions nothing
  reads go (`cc_dead_defs()`), liveness followed to an asm goto's
  labels too, and what that leaves one value folds again.
- `inline.c`: the inlining itself -- which functions may be inlined
  (`cc_inline_candidate()`: static and inline or always_inline, small --
  inline alone no bigger than about a call, `INLINE_MAX_COST`, but at a
  call whose constant arguments, bound in a copy of it folded on its own
  (`cc_optimize_function()`), bring it to that, `hint_folds_to_small()`,
  or at a call passing the address of a local of the caller that only
  calls of inline functions take, up to gcc's max-inline-insns-single,
  `LOCAL_ARG_MAX_COST` (`takes_local_slot()`: inlined, what it loads and
  stores through the pointer are the local's fields, as gcc reckons
  them gone; the calls such a body brings in are weighed again in the
  caller, a few rounds, `INLINE_LOCAL_ROUNDS` in `opt.c`) --
  an interface of plain values or structures by value -- an argument's
  storage taken for the parameter where the body only reads it, a result
  stored to the call's buffer, its words or a copy from where it points
  -- no frame-dependent body; a static function not declared inline
  whose body is no bigger than a call; or, as gcc's
  `-finline-functions-called-once`, any static function called from one
  place, named nowhere else and on no cycle of calls, which `opt.c`
  marks, if it does not grow the caller's frame past gcc's limit -- a
  frame reckoned by the slots that stay memory, `mem.c`'s
  `cc_ssa_memory_slot_bytes()`, and a few spills, the values being the
  register allocator's) and
  the copy of a body in place of a call (`cc_inline_calls()`), which
  hands the caller the callee's `keep_syms` too: the static locals
  marked `used` that the object keeps while some function still holds
  them, whether or not its code names them.
- `range.c`: what each integer value can hold, as an interval of its
  register's content (`cc_fold_ranges()`, run once the -O1 passes and
  inlining are done, before `resolve_isconst()`): from its definitions
  over the whole function -- constants, narrow loads, extensions, masks,
  sums that cannot overflow, a variable the union of its definitions' --
  and along the stream from the branches on a compare with a constant,
  into a label nothing else reaches, until the value is defined again or
  a label another way reaches (a label nothing reaches, whose code does
  not run, does not end it); a compare both operands' intervals answer
  one way folds to that answer.  `opt.c`'s `fold_module_ranges()` runs
  it over each function, gathering what every direct call passes, then
  again over each static function only direct calls reach
  (`mark_only_called()`, which `constant_args()` shares) with its
  parameters within what they are passed -- not negative, within their
  types -- as gcc's interprocedural range propagation.

Not here yet: mem2reg, CSE of computations across blocks (GVN; loads
only, above), LICM and loop transforms.
