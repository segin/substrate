# Architecture Overview

This document is the living architecture baseline for the Substrate native C compiler in `usr.bin/cc/`.
It explains how the compiler is partitioned into driver, frontend, middle-end, and backend stages, and how those stages interact with the in-tree assembler and linker.

## 1. Project Structure

```text
usr.bin/cc/
├── cmd/
│   ├── cc.c                 # Driver, tool resolution, stage orchestration, fork/exec glue
│   └── pipeline.c           # C -> IR -> assembly coordination
├── common/
│   ├── asm_constraint.c     # Inline-asm constraint classification
│   ├── diag.c               # Diagnostics: record the first error, print it with its source line
│   ├── lang.c               # C language modes: the one decoder of -std=
│   ├── options.c            # The settings of one compilation (cc_options_t)
│   ├── target.c             # Per-target data layout (psABI sizes and alignments)
│   └── util.c               # String duplication and string vectors
├── frontend/
│   ├── preproc.c            # Preprocessor helpers and expansion bounds
│   ├── lexer.c              # C tokenization
│   ├── parser.c             # Recursive-descent parser: the syntax
│   ├── parse_sema.c         # What the parser must know of meaning: variables in scope, expression types
│   │                        # (typeof), constant expressions, initializers put in their one canonical
│   │                        # form (cc_parser.h: what the two share)
│   ├── sema.c               # Type checking and semantic validation, a check per expression kind
│   ├── sema_call.c          # Checking a call: builtins, callee, arguments (cc_sema.h: what the two share)
│   ├── builtin.c            # Builtin-function and intrinsic handling
│   ├── consteval.c          # Integer constant expressions: the one evaluator
│   ├── binary128.c          # _Float128 constants: binary128 encoding and folding (cc_binary128.h)
│   └── types.c              # Scalar type sizes and alignments, for every phase
├── middle/
│   ├── ast2ir.c             # AST -> SSA lowering: functions, statements, expressions, data
│   ├── lower_call.c         # Lowering a call: the builtins it expands, ordinary calls
│   │                        # (cc_lower.h: what the lowering files share)
│   ├── irbuild.c            # Appending IR instructions (cc_irbuild.h), for lowering and the ABI code
│   ├── abi_x86_64.c         # x86-64 SysV: how aggregates are passed and returned; va_arg
│   ├── abi_i386.c           # i386 SysV: the same (both reached through the target descriptor)
│   ├── legalize.c           # IR legality checks; expands EXT (integer conversions) for the target
│   ├── legalize_i386.c      # i386: 64-bit integer operations as 32-bit word operations, division by libcall
│   ├── ssa_dump.c           # SSA -> the ir/ text form, for cc -fdump-ir
│   ├── passes/opt.c         # Optimization passes
│   ├── passes/inline.c      # Inlining of small static inline functions (-O1 and up)
│   ├── passes/mem.c         # Non-escaping stack slots: forwarding, splitting, value sets (-O1 and up)
│   └── ssa/                 # SSA module/function/block/instruction infrastructure
├── backend/
│   ├── regalloc.c           # Register allocation and spill planning
│   ├── frame.c              # Stack frame layout and slot compaction
│   └── x86/                 # GAS-style assembly emission (include/cc_x86.h)
│       ├── common.c         # Frame layout, register cache, inline asm, data, cc_emit_gas()
│       ├── x86_64.c         # x86-64 instruction selection and calling convention
│       ├── i386.c           # i386 instruction selection and calling convention
│       ├── regalloc_i386.c  # i386: the values given %ebx, %esi or %edi as their home
│       ├── mir.c            # x86 machine IR (include/cc_x86_mir.h): instructions, operands, printing
│       ├── linscan.c        # Linear-scan register allocation over machine IR
│       ├── mir_sel.c        # Legal IR to machine IR, what both targets share; frame, prologue, epilogue
│       ├── mir_i386.c       # i386's part of it: parameters, calls, returns (cdecl, regparm)
│       └── mir_x86_64.c     # x86-64's part of it: the System V psABI's parameters, calls, returns
├── include/                 # Internal driver/frontend/backend headers
├── ir/                      # The textual SSA IR (typed, LLVM-like; tests/usr.bin/cc/valid.ir)
│   ├── ir.c / ir.h          # Module representation
│   ├── parse.c              # Text -> module
│   ├── verify.c             # CFG and SSA invariants (terminators, phi arity, dominance, reachability)
│   │                        # and the opcode table: each opcode's form and its operands' type classes,
│   │                        # and for neutral IR (`stage neutral`) the integer width rules
│   ├── serialize.c          # Module -> text, optionally with value names normalized
│   ├── ir-verifier.c        # Tool: parse and verify
│   ├── ir-normalize.c       # Tool: parse, verify, print normalized
│   └── ir-diff.c            # Tool: compare two files after normalizing
│                            # (cc -fdump-ir writes this form: middle/ssa_dump.c)
├── README.md                # Current capability/status overview
├── SPEC.md                  # Functional requirements
├── Makefile                 # Native/target build-mode split and utility builds
└── docs/                    # building-linux.md

tests/usr.bin/cc/
├── conformance_c99/         # C99 conformance inputs
├── diff_c99/                # Differential comparison inputs
├── native_*.c               # Positive and negative compiler coverage corpus
├── run_*.sh                 # Mode matrices, preprocessor, post-C99, ABI, and SSA scripts
├── ir/                      # Golden IR: NAME.c and its -fdump-ir at -m64/-m32 (run_ir_golden.sh)
└── invalid_*.ir / valid*.ir # IR verifier fixtures
```

## 2. High-Level System Diagram

```text
C Source
  -> preprocessing
  -> lex / parse / sema
  -> AST -> SSA IR
  -> optimization + legalization
  -> instruction selection / regalloc / frame lowering
  -> GAS-style assembly (.s)
  -> as
  -> ELF object (.o)
  -> ld
  -> executable / shared object

Driver control plane:
  cc resolves sibling tools first (../as/as, ../ld/ld),
  then falls back to PATH tools or explicit environment overrides.
```

## 3. Core Components

### 3.1. Driver And Toolchain Orchestration

Name: `cmd/cc.c` and `cmd/pipeline.c`

Description: The driver parses user-facing flags, chooses stage stop points (`-E`, `-S`, `-c`, full link), resolves companion tools, and coordinates subprocess execution. It is the boundary between pure compilation logic and host/target toolchain integration.

Technologies: C, fork/exec, argument-vector construction, mode-aware tool resolution

Deployment: Installed as `usr/bin/cc`; `cpp` is exposed as a symlink to the same binary

### 3.2. Frontend

Name: Preprocessor, lexer, parser, and semantic analysis

Description: The frontend turns C translation units into typed ASTs. `frontend/preproc.c` enforces macro-expansion bounds, `frontend/parser.c` owns recursive-descent parsing (the semantics C needs during parsing -- which names are variables, the type `typeof` names, the value of an array bound, enumerator or case label -- are `frontend/parse_sema.c`'s, behind `include/cc_parser.h`; trigraphs are the preprocessor's alone).  An initializer list is read as written, designations kept, and `parser_canon_initializer()` puts it, with the type it initializes in hand, in one form: every designator resolved, every elided brace restored, every aggregate a positional list of its parts and a union's list its one member's initializer; that form, and the bound it gives an array of unknown bound, is all sema and lowering see.  Then `frontend/sema.c` applies type and language rules before lowering begins: it records every expression's type, makes every implicit arithmetic conversion an explicit cast (operands, `?:` arms, assignments, initializers, returns, arguments and their default promotions), and folds every `sizeof` whose size is known, so lowering reads types and conversions and never works them out. `frontend/types.c` is the one place the size and alignment of a scalar type are decided, from the target description in `common/target.c` (`cc_target_select()` in the pipeline picks it); structure layout, `sizeof`, `_Alignof`, lowering and the emitter all ask it. `frontend/consteval.c` evaluates integer constant expressions (array bounds, enumerators, case labels, `_Static_assert`, static initializers) for the parser, sema and lowering alike; the parser and sema each supply the size of a `sizeof` operand as they know it, and lowering, which sees only folded ones, supplies none. `frontend/nested.c` says what a GNU nested function sees of the function it is defined in (sema puts those variables in an outer scope; lowering passes an array of their addresses in the static chain register, `%r10` or `%ecx`, when the nested function uses them, and its address is a trampoline built in the caller's frame). `frontend/decimal.c` reads decimal floating constants and encodes them in BID, gcc's encoding on x86, for the parser's literals and sema's constant conversions; the decimal types themselves are structures holding that encoding, operated on by libgcc's `__bid_*` functions. `frontend/binary128.c` does the same for `_Float128` (`__float128`), IEEE binary128 on both targets: it encodes constants from their text, from integers and from binary floating constants, rounded once, and folds `+ - * /` on them as gcc does; the type is a structure holding the encoding (`cc_struct_def_t.binary_bits`), passed as gcc passes it and operated on by libgcc's soft-fp (`__addtf3`, `__extenddftf2`, `__fixtfdi` and kin), sema's decimal functions handing it to its own rules.

Technologies: C, recursive-descent parsing, bounded macro expansion, semantic diagnostics

Deployment: Linked directly into the `cc` binary

### 3.3. Middle-End And SSA Infrastructure

Name: AST-to-IR lowering, legalization, optimization, and shared SSA utilities

Description: `middle/ast2ir.c` lowers the frontend AST into the compiler's SSA-like IR, one function per expression kind (`lower_expr()` dispatches); a call is `middle/lower_call.c`'s, which expands the builtins the compiler implements itself (`lower_builtin_call()`; a `__builtin_memcpy`, `__builtin_memmove` or `__builtin_memset` of a constant size up to a few words is its loads and stores, no call, as gcc makes it) and makes ordinary calls (`lower_call_expr()`). `include/cc_lower.h` holds what the two share. Lowering does not decide how an aggregate crosses a call: it asks the selected target (`cc_abi_aggregate_arg()`, `cc_abi_aggregate_return()` in `include/cc_abi.h`, the descriptor's hooks), and `middle/abi_x86_64.c` (the psABI eightbyte classification) or `middle/abi_i386.c` (the stack, with gcc's vector registers) answers with the ABI byte the emitters act on; `va_arg` is theirs too (`cc_abi_va_arg()`), built with `middle/irbuild.c`, the IR builder lowering also uses. The trampoline a nested function's address needs, the full fence, the va_list record `va_copy` duplicates and which returned aggregates the emitter reads through their address are descriptor data, so lowering never asks which target it is compiling for. After the shared legalization, the target's own runs (the descriptor's `legalize` hook): on i386, `middle/legalize_i386.c` turns every 64-bit integer operation into operations on its 32-bit words (`lo32`, `hi32`, `pair`, `umulw`, legal IR only) and 64-bit division into a shift for a constant power-of-two divisor and `__divdi3`/`__udivdi3` calls otherwise, so the i386 emitter selects word instructions and only moves 64-bit values. Legalization runs before the optimizer, so `opt.c` runs the target's hook once more at the end of a function's passes: a call whose divisor it has since found constant (a local holding it, as Linux's `roundup()` writes) is made the shift or one 32-bit division then, since Linux does not link libgcc. `middle/legalize.c` and `middle/passes/opt.c` normalize and optimize that IR so backend selection runs on constrained, verifiable input; from -O1, `opt.c` also inlines small static inline functions bottom-up over the call graph (`middle/passes/inline.c`) and folds again, so a call guarded by a helper that is constant in the build goes with its dead branch, as with gcc; `middle/passes/mem.c` follows the function's own stack slots whose addresses do not escape (a slot stored once read as its value, a small structure split into its fields, the constants a slot can hold followed over the blocks), so a state kept in a local structure across a loop decides the branches on it.

Technologies: C, SSA-style IR, verifier-driven invariants, simple optimization passes

Deployment: Shared between `cc` and standalone IR utilities (`ir-verifier`, `ir-normalize`, `ir-diff`)

### 3.4. Backend

Name: Instruction selection, register allocation, frame layout, and assembly emission

Description: The backend converts legalized IR into GAS-style assembly for supported ABIs. `backend/x86/` writes the `.s` stream: `common.c` holds what both targets share (the frame's slots, the integer register cache, inline asm, sections, data and string literals) and `cc_emit_gas()`, which writes the module's data and its file-scope `asm("...")` text, as written and in source order, and hands each function to `x86_64.c` or `i386.c`, which select instructions from legal IR for their target (`include/cc_x86.h` declares what the three share); `backend/regalloc.c` chooses which register to spill and `backend/frame.c` checks and aligns frame sizes for it. On i386 the emitter keeps values in registers rather than in their slots: `common.c`'s register cache holds %eax, %ecx and %edx, a constant or a global's address is made where it is read, an address computed once is folded into the loads and stores through it, the values that pay for it most (by uses, weighted by loop depth) get %ebx, %esi or %edi as their home for the whole function (`regalloc_i386.c`, over liveness it works out), and the cache's contents are carried into a block reached only from the one before. A value takes a frame slot only for its live range, worked out over the blocks (`x86_value_spans()`), so values whose ranges do not meet share one, and one is not written back past its range (`slot_end`); in a frame of 8-byte slots, which a function with a 64-bit value has, a word takes a 4-byte slot of its own pool. Linux's kernel stacks are 8 KiB, and its frames are kept under 1 KiB. Each function is written to a scratch file first and copied out through text passes: shift and mask extensions made movsx/movzx, stores to slots nothing reads dropped and the slots left packed nearest %ebp, and a store followed by a load of the same slot made a move. Linux's 16-bit setup code, which must fit 32 KiB, is what this is measured against.

Before that path, `i386.c` and `x86_64.c` hand each function to the machine-IR path, which takes every one whose instructions it handles (all integer code: calls, structures passed and returned by value -- one of no whole access's size, 3, 5, 6 or 7 bytes, loaded and stored in pieces (`xs_load_bytes()`, `xs_store_bytes()`), an empty one nothing in registers -- inline asm, and variadic functions -- on x86-64 the register save area stored at entry, its %xmm half only when %al says the caller passed any and never under `-mno-sse`, and `va_start` filling the record a `va_list` points at; alloca and variable-length arrays, which move the stack pointer -- a loop's array given back its storage each time round through its save slot -- while the frame stays addressed from the frame pointer, and the epilogue makes the stack pointer again from it; a label's address, which a computed goto compares, a local symbol's; GNU nested functions, the environment read from the static chain register (%ecx, %r10) at entry and loaded there before a call of one; i386 `-fPIC`, the GOT's address made once at entry by a pc thunk (`XI_GOTBASE`, dropped when nothing reads it), data off it by @GOTOFF or through @GOT, and a call that may bind outside the object through @PLT with the GOT in %ebx; and floating point (`mir_fp.c`): on x86-64 a float or a double is a virtual vector register, which the allocator gives a %xmm register as it gives general ones (a register class per virtual register, spill slots and reloads by class), a float held as the double it rounds to; a long double, and on i386 every floating value (but floats and doubles under -mfpmath=sse or -msse2, which are vector registers there too, %xmm0-7), lives in a frame slot of its own and each operation is a short x87 sequence pushing its operands and popping the result back, nothing left on the x87 stack between IR instructions; compares are ucomisd's (fucompp, fnstsw and sahf on the x87) and setcc, false when unordered but for !=; x86-64 passes floats and doubles in %xmm0..7 (a float in single precision), a long double on the stack and back in %st0, a structure's SSE eightbytes in vector registers, and _Float16 through libgcc's __extendhfdf2 and __truncdfhf2 (on i386 the half pushed and the double back in %st0, the double pushed and the half back in %xmm0, through the target's `libcall` hook, the PLT under -fPIC); i386 stores a floating argument in its words on the stack (a float's one in single precision, a double's two, a long double's three) and takes a floating result from %st0, popping one nothing reads; its stack arguments are laid out as `i386.c` lays them -- a vector's copy aligned to its size from 16, a _Complex _Float128's to 16, the gaps pushed between -- vectors go in %xmm0-2 and %mm0-2 (`XI_MMLOAD`, `XI_MMSTORE`) and come back in %xmm0 or %mm0, a structure taken by value under regparm has its words, which came in %eax, %edx and %ecx, stored into a frame object of its own at entry, and a structure of a float or a double comes back in %st0; a structure returned through memory is copied to the caller's buffer, its address from %rdi on x86-64 and from the stack or %eax on i386, and that address returned) and returns 1 for the others, which are emitted as above. `mir_sel.c` selects legal IR into x86 machine IR (`include/cc_x86_mir.h`, `mir.c`; `mir_i386.c` and `mir_x86_64.c` add each target's parameters, calls and returns, an `xsel_target_t`): virtual registers, a 64-bit integer two of them, and the physical registers instructions name -- a call's arguments, result and clobbers, `idivl`'s %eax and %edx, a shift count's %cl. A constant defined once is an immediate where it is read, a global's address a symbol, a stack object's address and a pointer plus a constant are folded into the memory operands that use them, a compare only a branch reads is that branch's flags -- made at the branch, later in the block, when nothing between writes its operands, and read through copies to it (a variable set to the truth value) as well -- and an integer conversion -- legalization's shift left and back right, or an and with 0xff, 0xffff or 0xffffffff -- is one movsx or movzx, or nothing when every definition of the value leaves it so extended already (`compute_ext()`); inline asm has its register operands in registers: `x86_asm_plan()` (`common.c`) places each operand as `emit_inline_asm()` would -- an immediate, a slot, a value in a general register or a memory operand's address in one, a register chosen by its constraint, away from the clobbers and the other operands' -- the selector moves each input into its register before the asm and each output out after (`sel_asm_bound()`), the asm reading, writing and clobbering those alone, so the function keeps its values in registers across it and its prologue saves a callee-saved one the asm uses, and `emit_inline_asm()`, handed the plan, loads, stores and saves nothing of its own; memory operands, and the whole of an asm goto or of an asm with an x87, vector or register-pair operand, take theirs through frame slots, as `emit_inline_asm()` expects (which puts a floating operand where its constraint says, as gcc does: "x" in an %xmm register, a float rounded to single precision; "t", "u" and "f" at their x87 stack positions, the inputs pushed before the asm and, after it, the outputs popped into their values and the inputs it left on the stack -- neither clobbered nor matched to an output -- thrown away), an asm goto's outputs living in theirs -- and holding on every way out of it, as gcc's do: lowering gives each label an edge that stores them back, and `emit_inline_asm()` jumps to a stub of its own per label that stores the asm's registers and pops what it saved; and a call of `memcpy`, `memmove` or `memset` of a constant size up to a few words -- the copies and zeroings lowering makes for structures among them -- is its moves, as gcc makes it. On x86-64 a symbol's address is made off %rip, or read from the GOT as `x86_64.c` would. `linscan.c` then drops what nothing reads, coalesces copies within a block, and allocates the target's registers (i386 %eax, %ecx, %edx, %ebx, %esi and %edi; x86-64 all sixteen but %rsp and %rbp, and %xmm0..15) by linear scan over live intervals with holes, the physical registers' fixed ranges blocking them, each interval given the free register its copies suggest most (each copy to or from a physical register a vote by its loop depth, passed on along chains of copies between virtual registers); an interval no register is free for whole is spilled, its own or another's, whichever weighs less (accesses, tenfold per loop, per instruction spanned), and the spilled registers' operands become frame slots where the instruction takes memory and short reloads where it does not, until a round spills nothing. Spilled registers whose intervals do not meet share a slot; a parameter never written again stays in its incoming slot; and, once registers are physical, a spill slot is read within a block from the register that still holds what it was last stored from or loaded into (`forward_reloads()`), the reload a copy or gone. The frame is laid out last: the callee-saved registers used, pushed under the frame pointer, then the frame objects, the busiest nearest; a function with several returns and registers to restore has one epilogue, at its last return, which the others jump to.

Technologies: C, target-specific lowering for i386/x86_64, linear-scan style allocation, textual assembly emission

Deployment: Final in-process compilation stage before the driver invokes `as`

## 4. Data / Persistent Artifacts

### 4.1. Input Translation Units

Name: C source, headers, and preprocessor inputs

Type: Plain text source

Purpose: Provide the compiler with the language surface consumed by the frontend.

### 4.2. In-Memory IR

Name: AST, semantic state, SSA modules/functions/blocks, verifier metadata

Type: Process-local compiler data structures

Purpose: Carry the program through parsing, semantic validation, optimization, and backend lowering.

### 4.3. Intermediate Assembly

Name: GAS-style `.s` output

Type: Text assembly

Purpose: Bridge the native compiler backend to the in-tree assembler without inventing a second object writer inside `cc`.

### 4.4. Final Toolchain Outputs

Name: ELF objects, executables, and shared objects

Type: ELF files produced by `as` and `ld`

Purpose: Deliver artifacts compatible with the Substrate runtime and host-native validation workflows.

## 5. External Integrations / APIs

`as` and `ld`: The driver resolves sibling in-tree binaries first so host-native builds can exercise the full Substrate toolchain without installation.

Host preprocessor: Preprocessing support currently relies on a system `cpp` path for full macro/front-end coverage.

`--bootstrap-gcc`: A temporary compatibility path for source programs outside the currently supported native language slice.

Specifications: Functional requirements live in `SPEC.md`; capability/status notes live in `README.md`.

Tests: Compiler-facing regression surfaces live in `tests/usr.bin/cc/`.

## 6. Build, Installation & Invocation

Host/native mode: `make -C usr.bin/cc NATIVE_BUILD=1` builds `cc`, `cpp`, `ir-verifier`, `ir-normalize`, and `ir-diff` as host-runnable binaries.

Target mode: The default build links against Substrate CRT and libc and brands the output for the operating system image.

Build-mode isolation: `.build-mode` forces object and binary cleanup when switching between native and target builds so incompatible artifacts are not reused across modes.

Tool resolution order: environment override (`AS`, `LD`), sibling build-tree tool, same-directory tool, then PATH fallback.

## 7. Security Considerations

Recursive toolchain guard: The compiler participates in the same bounded toolchain-depth tracking as the assembler so shelling out across `cc`, `as`, and `ld` cannot recurse forever.

Preprocessor limits: Expansion depth, pass count, expanded text size, and expanded token count are bounded to cap hostile macro workloads.

Parser depth: Recursive parse depth is explicitly capped to prevent stack blowups from pathologically nested expressions or statements.

IR verification: Dedicated verifier utilities and legalization passes keep malformed or partially formed IR from silently reaching the backend.

Mode isolation: The `.build-mode` stamp prevents subtle native/target object reuse bugs, which are a correctness and diagnostic hazard during toolchain development.

## 8. Development & Testing Environment

Local build: `make -C usr.bin/cc NATIVE_BUILD=1`

Primary regression surface: `tests/usr.bin/cc/` contains positive/negative compile fixtures, conformance corpora, differential comparison suites, preprocessor mode scripts, ABI mode matrices, and IR verifier fixtures.

Standalone tools: `ir-verifier`, `ir-normalize`, and `ir-diff` make IR regressions easier to isolate than end-to-end compile failures alone.

Integration role: The compiler is the coordinator for real-world toolchain tests because it drives `as` and `ld` together on larger inputs.

## 9. Future Considerations / Roadmap

Structure: the refactor of the compiler as a whole is done -- one type representation, a typed AST from sema, one constant evaluator, a typed target-neutral IR with the ABI and legalization per target, thin emitters, and a compiler that builds itself (`run_selfhost.sh`).  Left from it: per-compilation state is still file-scope variables -- the interned type table (`frontend/ctype.c`), the language, target and settings (`common/lang.c`, `common/target.c`, `common/options.c`), and diagnostics' file names and label serials (`frontend/preproc.c`, `frontend/parser.c`, `frontend/sema.c`, `backend/x86/common.c`) -- where one context passed through the phases would let a process compile more than once.  A third target is possible and not scheduled.

Correctness: gcc (or clang, for the clang-compatibility tests) is the judge; a program cc compiles differently is a bug, fixed with a test under `tests/usr.bin/cc/` that fails before the fix.

## 10. Project Identification

Project Name: Substrate Native C Compiler

Repository Path: `usr.bin/cc/`

Primary Consumers: Direct user compilation, in-tree userland builds, external host-native package validation

Date of Last Update: 2026-04-22

## 11. Glossary / Acronyms

AST: Abstract Syntax Tree

SSA: Static Single Assignment

IR: Intermediate Representation

ABI: Application Binary Interface

Bootstrap GCC: Temporary fallback path that routes unsupported native compilation workloads through the host GCC toolchain