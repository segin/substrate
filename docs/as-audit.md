# Audit of the in-tree assembler (`usr.bin/as`) — requirements checklist

Audit of the hand-written assembler restored in `3169d142b`, carried out
2026-10-09 at `d74c9b6e3`.  The assembler is 44 C files, 41,308 lines:
`as.c` (driver and the textual passes before parsing), `as_lexer.c`,
`as_parser.c`, `as_symtab.c`, `as_sections.c`, `as_data.c`, `as_relax.c`,
`as_elf_emit.c` (15,093 lines: the object writer **and** most x86
instruction selection), `as_x86_encode.c` (6,466) with eighteen x86
extension files, and seventeen ARM and AArch64 files.  It writes ELF
through `usr.lib/elfobj`.  Line numbers are those of that commit.

Every finding is written as the requirement the code does not meet, in
EARS form: **ubiquitous** ("The assembler shall …"), **event-driven**
("When …"), **state-driven** ("While …"), **unwanted behaviour** ("If …,
then …") and **optional feature** ("Where …").  A box is ticked when the
requirement is met and a test shows it.

- **Evidence** is where the code falls short and what happens.
- **Basis**: *reproduced* — run, against the host-built assembler, with GNU
  `as` or `llvm-mc` as the reference; *traced* — the code path followed in
  full; *suspected* — plausible, not every path followed; *measured* —
  timed.
- A finding that names a source line as its input can be re-run with
  `as --32` / `as --64` on a file holding that line under `.text`.

How it was done: seven reviewers took one area each — front end;
symbols, sections, data and relaxation; the core x86 encoder; each half
of `as_elf_emit.c`; the x86 extension files; ARM and AArch64 — working
differentially against the host's GNU assembler wherever there was one,
with an AddressSanitizer build for hostile input.  The headline findings
of each were then re-run by hand before being written down here.

Nothing here has been fixed.

## 0. Summary

126 requirements the assembler does not meet, in eight areas.

**It does not assemble what a compiler writes.**  A small C program
through gcc, in fifteen combinations of mode and options, failed in every
one.  In 64-bit mode a misplaced brace (AS-X86-001) has, since April
2026, made the encoder refuse `test`, `xchg`, `cmpxchg`, `cpuid`, the
rotates, one-operand shifts, all of SSE and SSE2, and the x87 stack
forms, after encoding each correctly.  Against GNU `as`, identical output
for 56 % of a general-instruction corpus in 32-bit mode and 38 % in
64-bit; of 52,000 valid SIMD lines, 15,500 assemble to something else.

**What it does assemble is often wrong, with exit status 0.**  The worst,
each re-run by hand:

| | What is written | What is assembled |
|---|---|---|
| AS-SEL-001 | `lock cmpxchgl %ebx,(%eax)` | no `lock` prefix |
| AS-OBJ-001 | `movl $5, sym` | the 5 overwritten by the relocation |
| AS-OBJ-002 | `movaps sym(%ebx),%xmm0` | four zero bytes |
| AS-X86-010 | `mov %al,%cl` | `movl %eax,%ecx` |
| AS-X86-020 | `adcb %al,%al` | `cmpb %al,%al` |
| AS-X86-021 | `ret $4` | `ret` |
| AS-X86-022 | `movb %sil,%dil` (64-bit) | `mov %dh,%bh` |
| AS-X86-012 | `movb $1,%al` (64-bit) | three stray bytes after it, executed |
| AS-FE-002 | `.p2align 4,,10` | fifteen bytes of `0a`, executed |
| AS-FE-003 | `jmp 1f`, no `1:` | nothing; the jump is omitted |
| AS-FE-005 | `call *foo` | a direct call of `foo` |
| AS-LAY-001 | a branch over `.p2align` | a jump into the padding |
| AS-SEC-001 | `.pushsection .text, 1` | the whole of `.text` discarded |
| AS-OBJ-009 | code in `.init` | nothing |
| AS-OBJ-008 | `.int 5` | nothing |
| AS-ARM-003 | `call sp` (a function named `sp`) | `call *%esp` |
| AS-EXT-004 | `pshufb %fs:(%rax),%xmm2` | no segment prefix |
| AS-EXT-006 | `vandpd 8(%rsp),…` (EVEX) | reads `0x80(%rsp)` |
| AS-SEL-009 | `nopl (%eax)` | opcode `0F 1D` for `0F 1F` |
| AS-FE-001 | `.include "f"` | nothing, for a file that exists or does not |

i386 position-independent code and every TLS model but one are wrong
(AS-OBJ-003); `.cfi_*` produces empty unwind tables (AS-OBJ-010);
`.ifdef` is always false (AS-FE-007).

**It accepts what is not valid.**  Unknown directives and options,
`.error`, operands of the wrong size or class (`movl %ax,%ebx`,
`bsf %xmm1,%xmm2`), 64-bit instructions in 32-bit mode and the reverse,
values that do not fit their fields.

**Two of its four targets do not exist.**  `arm-as` and `aarch64-as` are
the x86 assembler under other names, and write x86 objects; the seventeen
ARM and AArch64 files are tables of fixed words that nothing calls
(AS-ARM-001, 004).

**It is slow, and in places quadratic or cubic.**  Thirty times GNU on
plain input; 8,000 sections take 12 seconds to GNU's 0.13 (AS-PERF-001 to
003).

**Memory safety is mostly sound.**  The sanitizer builds found one
out-of-bounds read (AS-FE-020), one null dereference (AS-X86-040),
unbounded recursion on long expressions (AS-FE-021) and a division trap
(AS-SEL-013), in tens of thousands of hostile and ordinary lines.

**The causes are few.**  Nearly all of the above comes from six things:

1. **No fixup records** (AS-DES-001).  The encoders return bytes; where
   a relocation goes is guessed afterwards from the instruction's length.
2. **No single layout** (AS-DES-002).  Five passes each encode
   everything again, and 2,000 lines predict what they will produce.
3. **Two x86 encoders** (AS-DES-004, 006), one of them 7,500 lines inside
   the file named for the object writer, running first and shadowing the
   other; instructions found by trying encoders in turn, not by name.
4. **No operand model** with a register's class and width (AS-X86-010).
5. **A textual front end** (AS-FE-050, 051): three passes through
   temporary files before any symbol exists, and a lexer that splits on
   white space only.
6. **At least six expression evaluators** with different grammars
   (AS-DES-005).  This one is done: they are `as_expr.c` now.

And the test suite it was written against — 147 files — was deleted with
it and not brought back (AS-TST-001).

By area: tests and documents 4; ARM and AArch64 8; front end 27; object
writer, relocations and data 18, with 3 on time and 6 on design; x86
selection in the emitter 14; layout, sections, symbols and data 18; the
core x86 encoder 18; the extensions 10.

## 1. Tests and documents

- [ ] **AS-TST-001** The assembler shall have a test suite in the tree that exercises it.
  Evidence: `tests/usr.bin/as/` holds two scripts, `test_branch.sh` and `test_mov64.sh`, both written in October 2026 for two bugs.  The suite the assembler was developed against — 147 files, 164,175 lines: 44 `test_*_core.c` unit tests with their runners, 45 driver-level shell tests, and 15 instruction corpora of 150,480 lines generated from GNU `as` — was deleted with the tools in `f2e3fe0d4` and was **not** restored with the sources in `3169d142b`.  It is in history at `f2e3fe0d4^:tests/usr.bin/as`.
  Basis: reproduced (`git show --stat f2e3fe0d4 -- tests/usr.bin/as`; `git show --stat 3169d142b | grep -c tests/usr.bin/as` is 0).
- [ ] **AS-TST-002** While the old suite is absent, the state of the assembler against it shall be known.
  Evidence: the assembler's sources are byte-identical to those the suite was deleted beside (`git diff f2e3fe0d4^ 3169d142b -- usr.bin/as` touches two files, both later fixes).  Of the 29 driver-level shell tests that need only the assembler, run against a host build: **11 pass, 18 fail**.  One failure is the harness (it wants `usr.bin/ld/ld`); the other seventeen were not each triaged between a defect and a stale expectation, but those looked at are defects recorded below (`salq`, `xadd`, `movs` and `ins` operand forms, `.if`, include cycles, forced short jumps, error messages).  `TASKLIST_AS.md` has 24 unticked items and says the rollout test was already failing.
  Basis: reproduced.
- [ ] **AS-DOC-001** `usr.bin/as/ARCHITECTURE.md` shall describe the assembler that is there.
  Evidence: §1 lists `tests/usr.bin/as/test_*_core.c`, `test_cli_*.sh`, `test_*_roundtrip.sh`, `test_fuzz_matrix.sh` and `corpus/`, none of which exist; §3 and `--target-help` (`as.c:2406`) say ARMv7 and AArch64 are supported targets, which they are not (AS-ARM-001).
  Basis: reproduced.
- [ ] **AS-DOC-002** The specification (`docs/specs/as_spec.md`, 608 lines) and `as(1)` shall agree with the program.
  Evidence: `TASKLIST_AS.md:254-258` records both reconciliations as never done.  Not re-audited line by line here; the divergences found are noted with the findings they belong to.
  Basis: traced.

## 2. ARM and AArch64

Seventeen files, about 4,000 lines.  Reference: `llvm-mc` and
`llvm-objdump`, through a host harness that compiles the files unmodified
under ASan and UBSan, since no command line reaches them.

- [ ] **AS-ARM-001** When invoked as `arm-as` or `aarch64-as`, the assembler shall assemble for that target, or shall not be installed under those names.
  Evidence: `as.c:2232` sets `pcfg.arch = AS_PARSER_ARCH_X86` unconditionally and `as.c:2234` chooses between `EM_X86_64` and `EM_386` only; `argv[0]` is read for the usage text alone (`as.c:2514`).  No function of any `as_arm_*.c` or `as_a64_*.c` file is called from the driver, lexer, parser, relaxer or emitter: the seventeen files are dead at link time, and their only callers were the deleted unit tests.  `Makefile:38-39,45-46` installs `arm-as` and `aarch64-as` all the same.  `./arm-as` on `add r0, r0, r1` says `unknown x86 register: r0`.
  Basis: reproduced.
- [ ] **AS-ARM-002** If source cannot be assembled for the target the program was invoked for, then the assembler shall fail, and shall not write an object for another machine.
  Evidence: `./aarch64-as` on `nop` / `ret` exits 0 and writes an `EM_X86_64` object holding `90 c3`.  `hlt`, `mov sp, sp` (`8b e4`), `cmp sp, sp`, `push sp` likewise.  `.inst 0xd65f03c0` emits nothing; `.word 0xe12fff1e` emits two bytes.  `.arch`, `.syntax`, `.arm`, `.thumb`, `.cpu`, `.fpu`, `.eabi_attribute`, `.fnstart`, `.thumb_func`, `.ltorg`, `.pool` are accepted and ignored, as are `-mcpu=`, `--target=aarch64`, `-marm`, `-mthumb`, `-EL`.
  Basis: reproduced.
- [ ] **AS-ARM-003** While assembling x86, a symbol whose name is that of an ARM register shall be a symbol.
  Evidence: `as_lexer.c:288-308` (`is_arm_like_register_name`, applied without a target check at `:708`) and `as_parser.c:570` (`is_arm_register_text`, used at 1355, 1423, 1562, 1583, 1591, 1885, 1986, 2130) take `sp`, `lr`, `pc`, `cpsr`, `spsr`, `xzr`, `wzr` and any `r`/`w`/`x`/`q`/`d`/`s`/`v` followed by digits for a register.  **Silently wrong:** `call sp` assembles to `ff d4` (`call *%esp`) with no relocation, where it is a call of the function `sp`; `movl sp, %eax` to `8b c4`; `jmp sp` to `ff e4`; `call r12` in 64-bit mode to `41 ff d4`.  **Rejected:** `call s1`, `jmp x1`, `movl s1, %eax`, `movl %eax, d2`: `unknown x86 register: s1`.  Compiler output trips it: `int s1; int x1(void){return s1;}` through `gcc -m32 -fno-pie -S` does not assemble.
  Basis: reproduced (re-run by hand).
- [ ] **AS-ARM-004** When an ARM or AArch64 instruction is encoded, the encoding shall be of the operands given.
  Evidence: most of the "encoders" are tables of one fixed machine word per mnemonic: `as_arm_dataproc.c:209-333`, `as_arm_loadstore.c:118-146`, `as_arm_system.c:93-122`, `as_arm_vfp.c:77-124`, `as_arm_neon.c:57-176`, `as_a64_dp_imm.c:52-85`, `as_a64_dp_reg.c:52-113`, `as_a64_loadstore.c:52-102`, `as_a64_system.c:52-78`.  The AArch64 instruction structures hold a mnemonic and nothing else; the VFP one a mnemonic, a condition and one register.  `mul r4, r5, r6` yields `e0010392`, which is `mul r1, r2, r3`; `push {r4, lr}` yields `push {r4, r5}`; AArch64 `add` is always `91048c20` (`add x0, x1, #291`), `ldr` always `f9400820`.  `as_a64_simd.c` (193 rows) and `as_a64_v81.c` (213 rows) are keyed on the whole text of the instruction, matched by string comparison: any other register or spacing is "unsupported".  The words in the tables are right for their keys — all 406 text-keyed rows match `llvm-mc` — so these are fixtures for tests, not encoders.
  Basis: reproduced.
- [ ] **AS-ARM-005** If an operand cannot be encoded, then the encoder shall report it, and shall not substitute another instruction.
  Evidence: `as_arm_dataproc.c:355-383`: when the parametric encoder refuses, the mnemonic is looked up in the fixed table and success returned.  `add r0, r1, #0x101` (no rotation encodes it) yields `e0821003`, `add r1, r2, r3`; `mov r0, #0x12345` yields `mov r1, r2`; `movw r3, #0x10000` yields `movw r1, #0x1234`.
  Basis: reproduced.
- [ ] **AS-ARM-006** The parametric ARM code shall encode shifts, offsets, register lists and conditions as the architecture defines them.
  Evidence, each reproduced against `llvm-mc`: `lsr #32` and `asr #32` are refused and `lsr #0`, `asr #0`, `ror #0` are written raw, which are `lsr #32`, `asr #32` and `rrx` (`as_arm_encode.c:276-281`); register numbers are masked with `& 0xf` unchecked (`as_arm_dataproc.c:123-146,181`); `ldr rX, =const` emits `ldr rX, [pc, #0]` and then the literal, so the load reads the word after the literal and execution runs into it (`as_arm_loadstore.c:98-116`); negative load and store offsets cannot be expressed (`:69` sets U always; `:71` reads `pre_indexed ? 1 : 1`); `as_arm_ldm_mode_to_pu` maps the stack suffixes as for a store, so `ldmfd` would be `ldmdb` (`as_arm_encode.c:447-467`); the mnemonic splitter strips a trailing `s` and then any two-letter condition with no table of real mnemonics, so `teq` is `t` + EQ, `svc` is `s` + VC, `bls` is `bl` + S, `mrs` is `mr` + S, and UAL order `movseq` does not parse though `unified_syntax` defaults to 1 (`:181-200`, `:484`; the dead twin `as_parser.c:2273` has the same fault); conditional hints lose their condition (`as_arm_system.c:102-106`); Thumb `add` picks flag-setting or not by operand width, and returns its halves in the opposite order to Thumb-2 `orn` (`as_arm_encode.c:359-402`, `as_arm_dataproc.c:187-207`).
  Basis: reproduced.
- [ ] **AS-ARM-007** The AArch64 helpers shall validate what they are given.
  Evidence: `ret` with no register is `ret x0`, not `x30`, and the register check runs before the mnemonic is matched, so `cbz` with a stale field fails as "BR/BLR/RET register out of range"; conditional branches exist only under the literal name `b.cond` (`as_a64_branch.c`); `x05` and `x007` parse as registers, `fp`, `lr`, `ip0` do not, and there is no parsing of any vector or FP register (`as_a64_encode.c:27-47,134-143`); `as_a64_reloc.c:3-5,78-80` invents `R_AARCH64_MOVW_UABS_G3_NC`, which the ABI does not have.
  Basis: reproduced; the relocation traced.
- [ ] **AS-ARM-008** When an ARM operand is refused, the message shall say why.
  Evidence: `as_arm_branch.c:204-214`, `as_arm_loadstore.c:195`, `as_arm_system.c:161` overwrite the specific message ("out of range", "must be 4-byte aligned") with `unsupported ARM … mnemonic: b`.
  Basis: reproduced.

What is right there: the rotated-immediate search, shifts by 1 to 31 and
by register, `movw`/`movt`, `ldr`/`str`/`ldrb`/`strb` with offsets 0 to
4095 and both index forms, `b`/`bl`/`bx`/`blx reg` with range and
alignment checks at the exact limits, AArch64 `b`/`bl`/`b.cond`/`cbz`/`tbz`
likewise, `svc`/`brk`/`hlt`, and `as_a64_encode_logical_imm` — 240,000
constants against an independent reference with no mismatch.  ASan and
UBSan found nothing in these files.

## 3. Front end: driver, textual passes, lexer, parser

`as.c`, `as_lexer.c`, `as_parser.c`, read in full.  The inputs given are
assembled with `--32` unless it says otherwise.

### 3.1 Wrong output, accepted

- [ ] **AS-FE-001** When a source has `.include "f"`, the assembler shall assemble the contents of f, or fail.
  Evidence: `as_lexer.c:966-989`.  The loop moves each token out with `as_token_vec_push_take`, which clears the one it took from (`:158`); the test for `.include` that follows reads the cleared token's kind, 0, and is never true.  The directive is skipped and the exit status is 0, for a file that exists and for one that does not.  `-I`, `--max-include-depth` and the cycle check cannot be reached.  `.include "inc2.s"` then `cli`, with `nop; hlt` in the file: `fa`, where it is `90 f4 fa`.  Behind it, when it is mended: includes would be looked for beside the `/tmp/ascond_*` file (`as.c:2243`), would skip the macro, repeat and conditional passes, and would have their local labels matched per file (`as_parser.c:2917`).
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-002** When a directive has an empty argument, the arguments after it shall keep their positions.
  Evidence: `as_parser.c:2259` drops an empty argument.  `.p2align 4,,10` — which gcc writes before every loop and function at `-O2` — becomes `.p2align 4,10`: `nop`, `.p2align 4,,10`, `nop` assembles to fifteen bytes of `0a` between the two (`or (%edx),%cl`, executed), where GNU pads nothing, fifteen being more than ten.  `.balign 8,,3` fills with 03; `.byte 1,,2` is `01 02`.  There is no maximum-skip in the emitter either (`as_elf_emit.c:13900-13932`).
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-003** If a local label reference (`1f`, `1b`) has no definition, then the assembler shall say so.
  Evidence: `as_parser.c:2932` records `local_resolved = 0` and nothing reads it as an error.  `cli` / `jmp 1f` / `call 2f` / `hlt` exits 0 with `fa e8 fa ff ff ff f4`: the `jmp` is **not in the output at all**, and the `call` goes to offset 0 of the section.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-004** A local label used as an immediate, an absolute address or a displacement shall give its address.
  Evidence: `resolve_locals` (`as_parser.c:2940-2969`) resolves by source line, and only branches and rip-relative operands honour that.  `pushl $1f` / `ret` / `1: nop` is `68 00 01 00 00` — push of 0x100, no relocation — where it is `68 06 00 00 00` with `R_386_32 .text`.  `movl $1f,%eax`, `leal 1f,%eax`, `movl 1f,%eax`, `movl 1f(%ebx),%eax` and `movq $1f,%rax` all emit 0 and no relocation.  In data, `.long 1b` makes an undefined global symbol named `1b`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-005** `*` before an operand that is not a register shall make the branch indirect.
  Evidence: `as_parser.c:2017-2054` removes the star and returns the operand as it would be without one.  `call *foo` is `e8 rel32`, a direct call of `foo`, where it is `ff 15 abs32`, a call through the pointer at `foo`; `jmp *foo` likewise; the same in 64-bit mode.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-006** Expressions shall be evaluated with the assembler's precedence, and with all its operators.
  In part: the parser is `as_expr.c`, with GNU `as`'s four ranks and `!`, `<>`, `&&`, `||`; `.long 1+2<<3, 1|1+1` is 0x11 and 2 wherever it is written; `.quad 0xffffffffffffffff` is itself; `.long 1 2`, `(4`, `12abc`, `09` are errors.  Still open, in the emitter's use of the result and not in the parsing: `.long x+y`, `2*x` assemble; `.long x - .` is 0 with a relocation that lacks its addend; `.long .+4` makes an undefined symbol `.`; an operand `$x-y` is a relocation against `x`.
  Evidence: `expr_precedence` (`as_parser.c:931-959`) is C's.  In GNU `as`, `* / % << >>` bind tightest, then `| & ^ !`, then `+ -` and the comparisons.  `.long 1+2<<3, 1|1+1` gives 0x18 and 3; GNU gives 0x11 and 2.  Unary `!`, `<>`, `&&`, `||` are "invalid operand".  Downstream of the parser: `movl $x-y+4` is a relocation against `x` with addend 0; `$x+y`, `$2*x`, `$-x` are accepted; `.long x - .` is 0 and `.long .+4` makes an undefined symbol named `.`; `.quad 0xffffffffffffffff` is `0x7fffffffffffffff`; `.long 1 2`, `(4`, `12abc`, `09` become undefined symbols of those names.
  Basis: reproduced (the precedence re-run by hand).
- [ ] **AS-FE-007** `.ifdef`, `.ifndef` and `.if` shall be decided by the symbol table and by arithmetic.
  Evidence: `as.c:1965-1978` makes `.ifdef` always false and `.ifndef` always true; `eval_gas_cond_expr` (`as.c:1854-1928`) knows a `strtoll` literal and otherwise compares text, or takes anything non-empty for true.  With `.set X,1`, `.ifdef X` is false.  `.if X` with X = 0 is true; `.if 2-1 == 1` false; `.if 0+0` true; `.if (0)` true; `.ifeq 1-1` false; `.if 1 & 2` true.  The conditional pass runs before there is a symbol table.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-008** The passes shall run in an order that respects conditionals, and a macro shall be definable once.
  Evidence: `run_native_backend` (`as.c:2205-2219`) expands macros, then repeats, then conditionals.  A `.macro` inside `.if 0` is defined, and the first definition of a name wins silently (`find_gas_macro`, `as.c:1131`).  A recursive macro that ends itself with `.if` never ends, and stops at a depth of 64 with "macro/control expansion stage failed".
  Basis: reproduced.
- [ ] **AS-FE-009** A `;` inside a comment or a string shall not begin a statement.
  Evidence: `write_substituted_line` (`as.c:793`) and `expand_split_statement` (`as.c:1511`) split on `;` before comments are removed, and the second ignores backslash escapes (`:1505-1510`).  `nop # c; hlt` assembles the `hlt`.  `.ascii "a\";b"` is "unterminated string literal"; a C string holding `\";` compiled by gcc fails so.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-010** If a directive is unknown, or is one that reports an error, then the assembler shall fail.
  Evidence: `validate_directives` (`as.c:202-234`) refuses twelve names; the parser takes any `.name` (`as_parser.c:2885`).  `.error "boom"`, `.err`, `.abort`, `.foobar 1,2`, `.exitm`, `.purgem`, `.altmacro` are accepted with status 0.  `.exitm` does not leave the macro.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-011** A memory operand shall be parsed as written, and what cannot be parsed shall be refused.
  Evidence: `as_parser.c:1349-1350` takes a base spelled `bad` for `%eax`: `movl (bad), %eax` is `8b 00`.  `as_parser.c:2616` skips empty operands and `parse_att_memory` ignores what follows `)` and a fourth component (`:1334-1346`): `movl $1,,%eax`, `movl $1,%eax,`, `movl (%eax)junk, %ebx`, `movl (%eax,%ebx,2,junk), %ecx` all assemble.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-012** Macros and repeats shall follow the directives' definitions.
  Evidence: `.rept 2 3` repeats five times (`eval_rept_count`, `as.c:919-965`, adds the numbers); `:vararg` and `:req` are discarded (`as.c:1363-1366`); an unterminated `.macro` or `.rept` at the end of the file is accepted (`as.c:1755`, `:986`); a quoted argument loses its quotes (`:1457`); `\@` starts at 1; `.string "a" "b"` is `a b`; nested `.macro` is ended by the inner `.endm`.
  Basis: reproduced.
- [ ] **AS-FE-013** Two labels on one line shall both be defined.
  Evidence: `as_lexer.c:840-865`: `q1:q2: .long 4` makes `q1` a label and `q2:` a mnemonic, "invalid operand '.long 4'".
  Basis: reproduced.
- [ ] **AS-FE-014** A command-line option shall take effect or be refused.
  Evidence: `--defsym X=5` is stored and never read (`as.c:2620-2636`): `.long X` is an undefined `X`.  Any unknown option is accepted (`as.c:2827-2834`): `--bogus-option` exits 0.  `--fatal-warnings`, `--warn`, `-g`, `-D` for a `.s`, `--from-cc` are stored and unused.  `-` for standard input is taken for an option, so `gcc -pipe` cannot work.  `-march=haswell` is refused (`as.c:517-571`).  `--version` prints `GNU assembler (GNU Binutils) 2.40` (`as.c:2846`), which tells a configure script it has features it has not.
  Basis: reproduced (re-run by hand).

### 3.2 Memory and limits

- [ ] **AS-FE-020** The lexer shall not read past the end of a line.
  Evidence: `as_lexer.c:735-742`: `strncmp(line+i, "{vex}", 6)` matches only when the terminator follows, and `i += 6` then steps over it.  A file holding the line `{vex}`: AddressSanitizer, heap-buffer-overflow in `tokenize_line` (`:725`).  For the same reason the prefix never matches where it was meant to, before an instruction.
  Basis: reproduced (re-run by hand).
- [x] **AS-FE-021** Expression depth shall be bounded however the expression is nested.
  Met: `as_expr.c` is the one parser of expressions, and bounds a tree at `AS_EXPR_MAX_NODES` (4096) besides its 256 levels of nesting, so every recursive walk is bounded with it; the recursive splitters in `as_data.c`, `as_sections.c` and `as_elf_emit.c` are gone.  A sum of 200,000 terms as an operand, of 400,000 in `.long`, and 100,000 nested parentheses are each "malformed", exit 1 (`tests/usr.bin/as/test_expr.c`, `test_exprsites.sh`).
  Evidence: `EXPR_MAX_DEPTH` (`as_parser.c:74`) counts parentheses and unary operators.  A left-leaning chain is unbounded, and `resolve_local_in_expr` (`:2936`) and `free_expr` (`:166`) walk it recursively: `movl $1+1+…` with 200,000 terms is a segmentation fault.  The same family elsewhere: `.long` of 100,000 terms faults in `as_data.c:180`, of 400,000 reached 25 GB resident before it was killed, and 20,000 nested parentheses fault in `as_elf_emit.c:805` (`const_expr_parse_xor`, no guard).
  Basis: reproduced.
- [ ] **AS-FE-022** Expansion shall be bounded.
  Evidence: `.rept`'s count is a `long` loop writing to a file in `/tmp` (`as.c:1000`, `:1701`).  `.rept 0x7fffffff` with an empty body runs 22 seconds; two nested `.rept 200000` wrote 413 MB before being stopped.  No `--max-*` limit covers expansion: all are checked on the original file, before any pass (`preflight_source_limits`, `as.c:394-462`).
  Basis: reproduced.
- [ ] **AS-FE-023** The documented limits shall mean what they say.
  Evidence: `--max-line-bytes` counts the newline; `--max-token-length` measures runs between spaces, a whole quoted string being one; `--max-macro-depth` counts textual nesting of `.macro`, which is not supported, and the real limit on expansion is a constant 64 (`as.c:1556`); `--max-line-bytes=-1` is accepted; `max_include_depth` is narrowed to `unsigned` (`as.c:2229`).
  Basis: reproduced; the narrowing traced.
- [ ] **AS-FE-024** Intermediate files shall be removed however the run ends, and shall go where the user's temporary files go.
  Evidence: three or four `mkstemp` files a run (`as.c:1019`, `1720`, `1933`, `2184`), removed only on the ordinary way out (`:2308-2319`); a crash or a signal leaves them.  The directory is `/tmp`, whatever `TMPDIR` says.
  Basis: reproduced.
- [ ] **AS-FE-025** Error paths shall free what they allocated, and a line shall be read whole.
  Evidence: `parse_operand_slice` leaks `op->raw` on every failing return (`as_parser.c:2009`, then `2105`, `2212`), and `parse_intel_memory` its segment register (`:1478-1497`).  What follows a NUL byte on a line is dropped (`as_lexer.c:444`).
  Basis: reproduced.

### 3.3 Messages

- [ ] **AS-FE-030** A message shall name the user's file and the line in it.
  Evidence: every message from the lexer on names `/tmp/ascond_XXXXXX` and a line of that file (`as.c:2243`); there is no map back.  A `;` in a comment, or a macro, moves every later line.  The `# N "file"` lines the C preprocessor writes are thrown away as comments, so a `.S` file's messages are wrong too.
  Basis: reproduced (re-run by hand).
- [ ] **AS-FE-031** A failure in a textual pass shall say what failed and where.
  Evidence: "macro/control expansion stage failed", "repeat expansion stage failed", "conditional assembly stage failed" (`as.c:2206-2216`) are the whole of it, for an unmatched `.endif`, an unclosed `.if`, a `.rept` whose count is a symbol, a bad `.macro` header and a dozen other things.
  Basis: reproduced.
- [ ] **AS-FE-032** A message shall have text; a failed run shall not leave the old output looking like the new.
  Evidence: `loop 1b` and `jecxz 1b` print `as: error: <file>:2: ` and nothing more.  On failure an output file already there is left as it was.  The default output is `a.out.o` (`as.c:2512`), and outputs are made mode 0600.
  Basis: reproduced.

### 3.4 Missing

- [ ] **AS-FE-040** The front end shall accept what compilers and hand-written sources ordinarily contain.
  Evidence: a prefix as a statement of its own — `lock; cmpxchgl …`, `rep; nop`, a bare `lock` — is "parse error" (`as_parser.c:2552-2580`); a character constant after `$` or inside an expression (`movb $'a',%al`) is not lexed (`as_lexer.c:796`); quoted symbol names, `%gs : 8` with spaces, backslash-newline continuation, standard input, more than one input file.
  Basis: reproduced.

### 3.5 Design

- [ ] **AS-FE-050** The source shall be read once, into statements that carry their file and line.
  Evidence: `as.c:745-2153`, some 1,400 lines, is a text preprocessor of three passes, each through a file in `/tmp`, each scanning quotes and comments its own way — there are six quote scanners (`as.c:786`, `1179`, `1256`, `1505`; `as_lexer.c:462`, `764`), two of which do not know escapes.  It cannot see symbols.  That is the root of AS-FE-007, 008, 009, 012, 022, 030 and 031.  `expand_rept_block` (`as.c:974`) and `expand_rept_block_with_macros` (`:1675`) are one function twice, as are `expand_rept_file` (`:1016`) and the repeat arm of `expand_gas_source_controls` (`:1770-1787`).
  Basis: traced.
- [ ] **AS-FE-051** The lexer shall produce tokens, and the parser shall not lex.
  Evidence: the lexer splits on white space only (`as_lexer.c:824`): `a+b`, `%fs:0`, `x=5`, `*%eax`, `$sym` are each one token.  The parser joins tokens back into strings (`join_tokens`) and lexes them again (`expr_lex_next`), with `strchr` for `:`, `=` and `*` in eight places more.  String tokens lose their quotes.  Statements are grouped by equality of file and line (`as_parser.c:2996`), which is why `;` has to be made into newlines textually.  `parse_instruction` (`:2521-2781`) holds two copies of an 80-line block (2654-2711, 2713-2770).  The register names are in three tables that disagree (`as_lexer.c:183`, `as_parser.c:304`, the encoder).
  Basis: traced.
- [ ] **AS-FE-052** Code that cannot run shall be removed.
  Evidence: the include machinery (`as_lexer.c:512-626`, `986-1025`); six `{vex}` filters; `march_to_gas` and the option strings built for a GNU `as` that is never run (`as.c:2875-2907`); `validate_directives`; the recursion guard; the `"bad"` case; the ARM arms of the parser.  `main` (`as.c:2502-2955`) repeats one three-line clean-up some forty-five times.
  Basis: traced.

Not reached: Intel-syntax operands (`as_parser.c:1399-1916`) were read and
fuzzed for crashes, not compared with GNU; the AVX-512 decorators and far
pointers likewise; the `.S` path through the C preprocessor, listings,
`-O binary` and `--statistics` were read and not run.

Right: numbers in operands in every radix up to 64 bits; `/* */`; `#` and
`;` inside strings; the escapes of `.ascii`; the label forms; `.set`
chains; ordinary macros with positional, default and named arguments;
`.irp`, `.irpc`, `.elseif`; segment overrides; `call *%eax` and
`*4(%eax,%ebx,4)`; a file with no final newline; a 5 MB string; branches
to local labels in one file.

## 4. Relocations, data, symbols and the object file (`as_elf_emit.c`, second half)

Lines 7500 to the end.  Read closely: the data directive emitters, the
instruction selector `encode_x86_stmt`, the text and data passes, symbol
emission, relocation emission and the top level (7684-8010, 9073-9465,
11477-12420, 12596-13200, 14372-15093).  Not read: the fifteen extension
wrappers (8011-9072), the "virtual layout" machinery (9470-11476) and the
flat-binary writer (13200-14371), which were exercised only through
tests.

**The first thing that breaks on a compiler's output:** a small C
program through gcc 16, in fifteen combinations of `-m32`/`-m64` with
`-O0`, `-O2`, `-Os`, `-fPIC`, `-g`, did not assemble in any of them
(`shrdl`, `faddp`, `imulq`, `rex64`).  No compiler-made object reached a
link.

### 4.1 Wrong output, accepted

- [ ] **AS-OBJ-001** When an instruction has a symbolic displacement followed by an immediate, the relocation shall be on the displacement and the immediate shall be kept.
  Evidence: `as_elf_emit.c:14851` computes the relocation's place as `end of instruction - width`, and `:14852-14863` writes the addend there.  `movl $5, sym` is `c7 05 00000000 00000000` with `R_386_32 sym` at offset 6: the immediate is overwritten and the instruction stores the address of `sym` at address 0.  GNU: the relocation at 2, the 5 kept.  `cmpl $0, sym`, `movb $1, sym` (a four-byte relocation over a one-byte immediate, running into the next instruction), `shll $2, sym`, `testl $1, sym+4` likewise.  In 64-bit mode `movl $5, sym(%rip)` has the same misplacement and an addend of -4 where the bytes after the field make it -8 (`:14838-14842`).
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-002** If an instruction is shorter than the relocation the emitter means to put in it, then the assembler shall fail, and shall not write over the opcode.
  Evidence: `as_elf_emit.c:14815-14828`, then the write at 14855/14861.  `movaps sym(%ebx), %xmm0` is emitted as `00 00 00 00` with the relocation at offset 0: the instruction is gone.  `movdqa sym(%ebp), %xmm1`, `flds sym(%ebx)`, and in 64-bit mode `movq $sym, 8(%rsp)` (`48` and eight zero bytes, `R_X86_64_64`) and `cmpq $gtab+64, %rax` the same.  The encoder, not knowing the operand is a symbol, chose a one-byte displacement or immediate.
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-003** A relocation operator shall give the relocation it names, or be refused.
  Evidence: `as_elf_emit.c:12137-12177` knows `@PLT`, `@GOTPCREL` and `@GOTTPOFF`, the last two for x86-64 only, by `strstr` on the symbol's name.  Anything else stays in the name: `leal bar@GOTOFF(%ebx), %eax` is `R_386_32` against a symbol called `bar@GOTOFF`.  So also `@GOT`, `@NTPOFF`, `@TLSGD`, `@TPOFF`, `@DTPOFF`, `@TLSLD`, `@TLSDESC`, and in data.  `addl $_GLOBAL_OFFSET_TABLE_, %ebx` is `R_386_32`, not `R_386_GOTPC`.  **All position-independent code for i386, and every TLS model but x86-64 initial-exec, is wrong.**
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-004** A reference to a local label in a mergeable section shall keep the label.
  Evidence: `as_elf_emit.c:13150-13165` rewrites every `.L` reference to section symbol plus offset, and the PC-relative -4 is then added to it (`:14838-14842`).  Three strings in `.rodata.str1.1,"aMS"` reached by `leaq .LCn(%rip)`: relocations `.rodata.str1.1 - 4`, `+2`, `+7`; the linker warns `access beyond end of merged section (-4)` and the program prints `||alpha` for `alpha|beta|gamma`.
  Basis: reproduced.
- [ ] **AS-OBJ-005** `.comm` and `.lcomm` shall give a common symbol its size and alignment, and `.local` shall be honoured.
  Evidence: `as_elf_emit.c:12967-12977` puts the size in `st_value` and sets no `st_size`; the alignment is unused.  `.comm bufa,64,32`: value 0x40, size 0.  After a link two such symbols and a `.local` one share one address with size 0 and there is no `.bss`; an `.lcomm` symbol stays common and local and is never given storage.
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-006** A data directive that holds a symbol shall get a relocation of the directive's width.
  Evidence: `as_elf_emit.c:14623`, `reloc_type_for_machine` ignores the width.  In 64-bit mode `.long ext` is `R_X86_64_64` on a four-byte field: the linker writes eight bytes, over the next word.  In 32-bit mode `.word ext` and `.byte ext` are `R_386_32`.  In 64-bit mode `.word ext` and `.byte ext` fail with `failed to emit relocations` and no file or line.
  Basis: reproduced.
- [ ] **AS-OBJ-007** If a data expression cannot be resolved, then the assembler shall fail.
  Evidence: `as_elf_emit.c:11715-11731` writes zeros for anything with a symbol in it, and `:14646-14653` makes a relocation only for a four-byte `symbol - .`.  `.long ext+4 - .`, `.long ext - h`, `.quad ext - .`: zeros, no relocation, status 0.  `.space` and `.skip` with a count that cannot be evaluated become 0 (`:11754-11757`), as does a negative count (`:11770`).
  Basis: reproduced; `.space` traced.
- [ ] **AS-OBJ-008** If a directive is not implemented, then the assembler shall say so.
  Evidence: `as_elf_emit.c:7873` and `:11800` return success for a name they do not know.  `.byte 0xEE` / `.int 5` / `.byte 0xFF` gives `ee ff`: **`.int` emits nothing.**  The same for `.value`, `.dc.*`, `.ds.*`, `.dcb.*`, `.octa`, `.uleb128`, `.sleb128`, `.single`, `.tfloat`, `.string16`, `.balignw`, `.struct`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-009** Instructions shall be assembled into whatever section they are written in.
  Evidence: `as_elf_emit.c:12039-12041`, `12683`, `12788`, `14725` skip a statement whose section is not flagged executable.  `.section .init` / `call f` / `ret`: a section of size 0, status 0.  `.fini`, `.section .foo,"a"`, `.data` followed by `nop`, `.gnu.linkonce.t.x` likewise.  Default flags are also off: `.init`/`.fini` are not `AX`; `.tdata`/`.tbss` lack `W` and `T` and `.tbss` is `PROGBITS`; `.init_array` is `PROGBITS`; `.debug_*`, `.comment`, `.eh_frame` are allocated; the `e` and `R` flags are dropped and `o` becomes `G`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-010** `.cfi_*` directives shall produce unwind tables, or be refused.
  Evidence: `as_elf_emit.c:15019-15032`, `12383-12401` notice that the directives are there and create the sections empty: `.eh_frame` of size 0 and alignment 4, an empty `.eh_frame_hdr` (which has no place in an object), no `.rela.eh_frame`, and an empty `.debug_line` because there was a `.file`.  With `-g`, `.debug_info` is corrupt ("Corrupt unit length").  Unwinding through such code, and C++ exceptions, are lost without a word.
  Basis: reproduced.
- [ ] **AS-OBJ-011** A symbol's attributes shall reach the symbol table.
  Evidence: `.symver f, f@@V1` leaves no versioned symbol (`:12920-12940`); `@gnu_indirect_function` and `@gnu_unique_object` become `NOTYPE` (`map_type`, `:12095-12107`); `.set alias, func` does not copy type or size; there is no `STT_FILE` symbol and `.ident` makes no `.comment`; a symbol defined with no recorded place is put at `.text+0` (`:12996`).
  Basis: reproduced; the last traced.
- [ ] **AS-OBJ-012** A reference to a local label shall find the right label however many there are.
  Evidence: `as_elf_emit.c:14532` is `local_emit_label_t local_labels[8192]` on the stack (some 1.4 MB); `:14565` stops recording without a word when it is full, and `:14493-14501` then takes the nearest one recorded.  9,000 lines of `1: .long 1b - .`: every entry after the 8,192nd has the wrong addend.
  Basis: reproduced.
- [ ] **AS-OBJ-013** A mnemonic's suffix shall not change what the instruction is, and operands shall not be dropped.
  Evidence: `as_elf_emit.c:9174`, `9199-9208`.  In 32-bit mode `movq %xmm0, (%eax)` is `89 00`, `mov %eax,(%eax)`, where it is `66 0f d6 00`; `movq (%edx), %xmm0` is `8b 02`.  `addq $1,%rax`, `movq %rax,%rbx`, `pushq %rax` are accepted in 32-bit mode as 32-bit operations.  **`ret $4` is `c3`**: the count is dropped, which breaks every callee-pops return.  `op_count` is clamped to 3 (`:9199`), so `ret $4, $5` and `nop %eax,%ebx,%ecx,%edx` are accepted.
  Basis: reproduced (re-run by hand).
- [ ] **AS-OBJ-014** The order in which encoders are tried shall not change the class of the encoding.
  Evidence: `as_elf_emit.c:9327-9380`: the AVX-512 encoders are tried first and are not gated.  In 32-bit mode `vaddps %ymm0,%ymm1,%ymm2` is `62 f1 74 28 58 d0`, an EVEX encoding that needs AVX-512VL, where it is `c5 f4 58 d0`.  In 64-bit mode by default the line is refused as needing `-march=x86-64-v3`.
  Basis: reproduced.
- [ ] **AS-OBJ-015** Relocation types for branches and immediates shall be the conventional ones.
  Evidence: `as_elf_emit.c:14372-14406`, `14433-14435`.  i386 `call foo` is `R_386_PLT32`, also against a local function GNU would resolve itself; x86-64 `jmp`/`jcc` are `PC32` not `PLT32`; `movl $sym,%eax` is `32S` not `32`; `@GOTPCREL` is never the relaxable `GOTPCRELX`; a `jcc` to an undefined `.L` symbol is a two-byte jump with an eight-bit relocation.  `movl $a, b` — two symbols in one instruction — is refused (`:14844`).
  Basis: reproduced.
- [ ] **AS-OBJ-016** Data values shall be as written.
  Evidence: `.quad 0xffffffffffffffff` is `…ff 7f` (a saturating signed parse, by `:11692`); `.ascii "a" "b"` is `a b`; `.bss` followed by `.long 5` is accepted; `.float 1e39` is infinity; `.zero`, `.space` and `.fill` above 1 MiB are refused (`:11776`, `:11664`), so a large static array cannot be assembled.
  Basis: reproduced; the limits traced.

### 4.2 The output file

- [ ] **AS-OBJ-020** The object shall be written where and as the user asked.
  Evidence: the output is mode 0600 (`mkstemp` and `rename` in `usr.lib/elfobj/src/elf_util.c:392-412`, no `fchmod`), and an existing read-only output is replaced; `-o /dev/null` fails with "invalid ELF format" (`as.c:2348`) and leaves its intermediate files; an output that is the input overwrites the source; a dangling symbolic link as output fails; `elf_write_file failed` gives no reason (`as_elf_emit.c:15039`).  (The linker had the same faults and was mended: LD-OUT in `docs/ld-audit.md`.)
  Basis: reproduced.
- [ ] **AS-OBJ-021** Names shall not be cut to fit a buffer.
  Evidence: `section_from_directive` has a static `secbuf[256]` (`:7878`): a 301-character section name is "unknown section".  `target_section[128]` (`:13116`, `13151`, `14463`, `14574`) would send a relocation to another section for a name over 127.  `emit_symbols` leaks on its early returns (`:12850`, `12963-12999`, `13022`, `13077-13091`); the result of `elf_symbol_define` is ignored at `:12252`.
  Basis: the first reproduced; the rest traced or suspected.

### 4.3 Time

- [ ] **AS-PERF-001** Assembly time shall be linear in statements, symbols, relocations and sections.
  Evidence (CPU seconds, ours against GNU):

  | Input | Ours | GNU |
  |---|---|---|
  | 200,000 `addl` and 20,000 labels | 11.97 (184 MB) | 0.38 (12 MB) |
  | 50,000 `movl extN,%eax` | 2.96 | 0.14 |
  | `.long extN` × 2,000 / 4,000 / 8,000 / 16,000 | 0.17 / 0.66 / 2.56 / 11.07 | 0.02 at 8,000 |
  | branches to `.L` labels × 2,000 / 8,000 / 32,000 | 0.12 / 0.95 / 15.04 | 0.02 at 8,000 |
  | one section a function × 2,000 / 4,000 / 8,000 | 1.35 / 5.20 / 22.45 | 0.03 / 0.07 / 0.14 |

  Symbolic data is quadratic: 92 % of the profile is `find_label_virtual_location` (`:9798`), which scans every statement for each argument.  Branches are quadratic in `eval_direct_local_branch_target`.  Sections are quadratic: `elf_find_section` by name through `section_for_name` (`:8896`) and `section_name_is_executable` (`:8909`), and `sec_buf_find` (`:7952`), per statement, in every pass.  The linear case is thirty times GNU's because each statement is encoded once in each of five passes (`emit_text_program`, `emit_data_program`, `collect_symbol_locations`, `collect_dot_locations`, `emit_relocations`), each encoding trying up to eleven extension encoders first (`:9322-9380`).
  Basis: measured (8,000 sections re-timed by hand: 12.3 s against 0.13 s; 8,000 `.long extN`: 2.4 s against 0.03 s).

### 4.4 Design

- [ ] **AS-DES-001** The encoder shall say where its relocations are.
  Evidence: there are no fixup records.  The place, width and type of a relocation are worked out again after encoding, from the mnemonic's spelling and the instruction's length (`as_elf_emit.c:14803-14851`).  An encoder that returned (offset, width, pc-relative, expression, bytes after the field) would remove AS-OBJ-001, 002, 006, 015 and the two-symbol limit as a class.
  Basis: traced.
- [ ] **AS-DES-002** There shall be one layout, computed once.
  Evidence: no table of (section, offset, bytes, fixups) per statement exists; five passes each encode everything again, and some 2,000 lines of "virtual layout" (`:9470-11476`) exist to predict what the passes will produce.  One table would remove the re-encoding, that machinery, the 8,192-entry label array (AS-OBJ-012) and the quadratic lookups (AS-PERF-001).
  Basis: traced.
- [ ] **AS-DES-003** One thing shall be implemented once, and the file shall hold what its name says.
  Evidence: three data-directive emitters (`:7684`, `11535`, `13818`), with `.align`, `.fill` and `.zero` done twice with different limits; the `.byte`/`.word`/`.long`/`.quad` width ladder four times, none including `.int`; the section walk copied into five passes; the clean-up in `as_elf_emit_file` twice (`:15043-15065`, `15069-15091`).  Relocation operators are found by `strstr` in the symbol name; sections are known by name string.  The file holds operand conversion, section buffers, fifteen extension wrappers, instruction selection (`encode_x86_stmt`, 312 lines), layout prediction, the passes, the symbol and relocation writers (`emit_relocations`, 373 lines) and a 900-line flat-binary writer.  Dead: `find_label_virtual_offset`, `prebuild_virtual_label_cache` (both marked unused), `append_directive_data_location_pass` (a forwarder), the `sb == NULL` branch at `:12051`.
  Basis: traced.

Right: the ELF header for both targets; `.rel.*` on i386 and `.rela.*` on
x86-64 with their links; locals first in `.symtab` with the right
`sh_info`; section symbols; visibility; weak undefined symbols; `.equ`;
`.size f,.-f`; merge sections' entry size and the group flag; i386
in-place addends for `sym+N` and `.L` labels; `.long a-b` in one section;
`lret $8`; in 64-bit mode `@PLT`, `@GOTPCREL`, `@GOTTPOFF`,
`sym+8(%rip)` and `movabsq $sym`.  A failed run leaves no partial file.

## 5. x86 instruction selection in `as_elf_emit.c` (first half)

Lines 1 to 7505 hold **no ELF writing at all**.  They are a second x86
encoder: register parsers, a ModRM/SIB emitter, some 45 `lookup_i386_*`
tables, and two `strcmp` ladders — `emit_i386_special` (5347-6952, 1,606
lines) and `emit_x86_64_special` (6953-7505) — which run **before**
`as_x86_encode.c` and can only decline by returning -1, so their mistakes
hide the real encoder.  Read closely: 146-1660, 2946-4130, 4233-4370,
4526-7683.  The opcode tables (1865-2945, 4131-4232, 4373-4525) were
checked by a differential sweep of 24,000 lines in 32-bit mode and 8,000
in 64-bit rather than line by line.

- [ ] **AS-SEL-001** When a `lock` prefix is written, it shall be emitted.
  Evidence: `emit_i386_prefixed_0f_rm` (`as_elf_emit.c:1660`) is not given the instruction, so its callers at 6182 (`cmpxchg`), 6222/6228 (`bt*`), 6426 (`cmpxchg8b`) and 6089/6095 (`shld`/`shrd`) lose the prefixes.  `lock cmpxchgl %ebx, (%eax)` is `0f b1 18`, where it is `f0 0f b1 18`.  `lock btsl`, `lock btrl`, `lock cmpxchg8b` likewise.  **An atomic operation is assembled as one that is not, and nothing says so.**
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-002** When a memory operand is `sym(base)`, the displacement shall be 32 bits.
  Evidence: `eval_abs_mem_disp` (`:1466-1478`) answers 0 for a symbol and `emit_i386_modrm_rm_operand` (`:1597-1603`) then chooses an eight-bit displacement; the three x86-64 copies do the same (`:3858`, `3948`, `4038`).  `convert_operand_x86` has it right, with `force_disp32` (`:7662`).  This is the cause of AS-OBJ-002, for everything that goes through the two ladders: SSE and MMX moves and arithmetic, `cmpxchg`, `movnti`, `movbe`, `fxsave`, `ldmxcsr`, `prefetch*`, `clflush`.  For one-byte opcodes it is an error instead: `flds sym(%ebx)`, `fldl`, `fildl`, `fnstcw` are "failed to emit relocations".
  Basis: reproduced.
- [ ] **AS-SEL-003** `movq` between MMX or XMM registers and memory shall be the SIMD move.
  Evidence: `normalize_x86_mnemonic` removes the `q` (`:3611`), so the `movq` block at 5981-5995 cannot be reached, and `parse_x86_reg` (`:1118-1140`) takes `xmmN` and `mmN` for general registers.  In 32-bit mode `movq %xmm1, %xmm2` is `8b d1`, `mov %ecx,%edx`.  (AS-OBJ-013 is the same fault seen from the memory forms.)
  Basis: reproduced.
- [ ] **AS-SEL-004** An immediate shall not be encoded as a memory address.
  Evidence: `emit_i386_modrm_rm_operand` (`:1504`) treats an immediate operand as a 32-bit displacement without looking for the `$`.  `psllw $1, %xmm1` is `66 0f f1 0d 01000000` — shift by the word at address 1 — where it is `66 0f 71 f1 01`.  The same for `pslld`, `psrlq`, `psraw`, `psrad`: the shift-by-immediate group `0F 71/72/73` is not implemented.  `flds $3`, `clflush $3`, `fxsave $3`, `prefetcht0 $3` and some sixty more that GNU refuses are accepted.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-005** A 16-bit operand shall have its operand-size prefix.
  Evidence: `emit_i386_special` calls `normalize_x86_mnemonic(..., NULL)` (`:5367`), throwing the suffix away, and does not look at register width.  `bsfw %ax,%bx` is `0f bc d8`, where it is `66 0f bc d8`.  The same for `bsr`, `popcnt`, `lzcnt`, `tzcnt`, `bt*`, `cmpxchg %bx,(%eax)`, `shld $4,%ax,%bx`, `movbe`, `sldtw`, `smsw %ax`, `rdrand %ax`, `pushw %fs`, `movw %cs,%ax`.  `cmpxchg %bh,(%eax)` uses the 32-bit opcode because `is_x86_low8_reg` (`:3786`) leaves out `ah`, `bh`, `ch`, `dh`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-006** `REX.W` shall be set when the operand is 64 bits and only then, and shall follow the mandatory prefix.
  Evidence: `:4812` is `rex = 0x48 | …` unconditionally: `cvtsi2sdl %eax, %xmm2` is `f2 48 0f 2a d0`, which converts `%rax`.  In 64-bit mode `paddb %xmm1,%xmm10` is `44 66 0f fc d1`: the REX byte is before the `66`, so it is ignored and the register is `%xmm2`.  `movq %rax,%xmm0` likewise.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-007** The x87 instructions shall take the operand orders compilers write, and the common ones shall exist.
  Evidence: `:5753-5768`, `5561-5571`, `5778-5792` require the destination to be `%st(0)`.  `faddp %st, %st(1)`, which gcc writes, is "unsupported mnemonic"; `fsubp %st(2), %st`, which GNU refuses, is accepted.  Missing: `faddp`/`fxch`/`fcom`/`fucom` with no operand, `fabs`, `fldz`, `fldpi`, `fsqrt`, `fsin`, `fcos`, `fpatan`, `frndint`, `fscale`, `ftst`, `fxam`, `fcompp`, `fninit`, `fnclex`, `fstsw`, `fstcw`, `fsave`, `fstenv`, `fildq`, `fistpq`.  `fisttp (%eax)` is the 32-bit form where GNU's default is 16.  In 64-bit mode the table has 8 entries to i386's 59 (`:4131`): `flds`, `fildl`, `fldcw`, `fld %st(1)`, `fxch`, `fldz` are refused.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-008** The opmask instructions shall be encoded as the manual gives them.
  Evidence: `emit_i386_vex2_kmovw` (`:4569`) always uses opcode `90`: `kmovw %eax,%k1` is `c5 f8 90 c8`, which is `kmovw %k0,%k1`, where it is `c5 f8 92 c8`.  `emit_i386_vex_klogic` (`:4605-4611`) takes the prefix and W bit from the last letter of the mnemonic, and `:5439`, `:7254` match by prefix: `kunpckbw` and `kunpckwd` are wrong, and `kand`, `kor`, `kandfoo`, `kaddz`, which do not exist, are accepted.  `kmovw %k2,%k1`, `kmovb/d/q`, `kortestw`, `knotw`, `kshiftlw` are refused.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-009** The tables shall hold the right opcodes.
  Evidence: `nopw`/`nopl` are `0F 1D` (`:6769`, `6910`), where they are `0F 1F` — **the multi-byte NOP every compiler pads with**; `aesencwide256kl` is `/0` for `/2` (`:6660`); `maskmovq` and `maskmovdqu` have their two registers the wrong way round (`:6388`, `6394`); `extrq` and `insertq` their two immediates (`:5081-5082`, `5109-5110`); `montmul` and `xstore-rng` lack bytes; `umonitor %cx` lacks its `67`; `pshufd $-1` is refused; `tpause` takes `%eax` and `%ax` but not `%ecx` (`:6152`).  `movzx %ax,%eax` is `0f b6`, the byte form; `movsxw %ax,%eax` is `66 0f be`; `crc32 %al,%ebx` uses opcode `f1` for `f0` and `crc32 %ax` lacks `66`.
  Basis: reproduced (`nopl` and `movzx` re-run by hand).
- [ ] **AS-SEL-010** The address size shall be honoured.
  Evidence: `parse_x86_reg` gives `bx`, `ebx` and `rbx` one number and the ladders' ModRM emitters never write `67` or look at the base's width.  In 32-bit mode `movaps (%bx),%xmm0` is `0f 28 03`, that is `(%ebx)`; in 64-bit mode `fldl (%eax)` is `dd 00`, that is `(%rax)`.  A scale of 3 is taken for 1 (`:1552-1561`).
  Basis: reproduced.
- [ ] **AS-SEL-011** A register shall be of the class and width the instruction takes.
  Evidence: `parse_x86_reg` (`:1118-1140`) succeeds for vector and MMX registers, and `emit_i386_modrm_any_rm_operand` (`:1646-1648`) takes any class in any place.  `bsf %xmm1,%xmm2`, `cmpxchg %xmm0,%xmm1`, `movd %xmm1,%xmm2`, `punpcklqdq %mm1,%mm2`, `shld $4,%al,%bl`, `movnti %eax,%ebx`, `sgdt %eax`, `invlpg %eax`, `cmpxchg8b %eax`, `sldt %rax` all assemble.  `movsd %xmm1,%mm2` is `a5` and `cmpsd $1,%xmm1,%xmm2` is `a7`: the string instructions, operands ignored.  1,752 such acceptances in the sweep.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-012** A mnemonic shall be accepted with the suffixes it takes.
  Evidence: `normalize_x86_mnemonic` and `is_size_suffixable_base` (`:3319-3349`) do not know `sal` with a suffix (`sall $1, %eax` is "unsupported mnemonic"), `shldl`/`shrdl`, `larl`/`lsll`, `movntil`, `boundl`, `lssl` and its fellows, `iretl`, `lretl`, `movbew`/`movbel`, `adcxl`; nor `shld %eax,%ebx` with none.
  Basis: reproduced (re-run by hand).
- [x] **AS-SEL-013** Constant arithmetic shall not kill the assembler.
  Met: the three evaluators named are one, `as_expr_eval`, whose arithmetic is done unsigned on 64 bits: `INT64_MIN / -1` is its dividend, a shift by 64 or more or by a negative count is 0, a division by zero is an error returned.  `test_expr.c` holds each; nothing is reported by the sanitizer build over it.
  Evidence: `eval_expr_const` (`:874`, `894`, `898`, `902`), the same in `eval_expr_asm_vars` (`:983-984`) and `const_expr_parse_*` (`:671`, `699`, `707`, `715`).  `movl $-9223372036854775808/-1, %eax` ends the assembler with SIGFPE; UBSan flags the negation and the multiplication.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEL-014** Port I/O and a few one-byte forms shall be the size written.
  Evidence, seen from this half and belonging to the dispatch around it: `in %dx,%al` is `ed` and `out %al,%dx` is `ef`, the 32-bit forms, where they are `ec` and `ee` (width is inferred only in Intel syntax, `:9261`); `enter $8,$0` is `c8 00 00 08`; `nop (%eax)` is `90`; `push %ax`, `iretw`, `lretw`, `pushaw`, `cbtw`, `cwtd` lack `66`; `xchgb %al,(%eax)` is `87`; string instructions are given explicit default segment overrides (`movsb` is `26 a4`); bare `movsb`, `rep movsb`, `insb`, `outsb`, `xlat`, `aam`, `aad` are refused in 32-bit mode.
  Basis: reproduced (`in %dx,%al` re-run by hand).
- [ ] **AS-PERF-002** A statement shall be encoded once, by a lookup.
  Evidence: callgrind on 3,000 instructions: 132,000 machine instructions for each source instruction.  `encode_x86_stmt_for_layout` runs four times for each; each run normalises the mnemonic twice, walks all of `emit_i386_special` (some 300 `strcmp`s and 45 table scans) and each extension's ladder before reaching the real encoder; `streq_ci` is called 316 times an instruction.  Half the time is in libc's `strcmp`.  200,000 lines: 9 to 11 seconds against 0.4.  Nothing in this half is quadratic.
  Basis: measured.
- [ ] **AS-DES-004** There shall be one x86 encoder.
  Evidence: ModRM/SIB is implemented here (`:1480`) and again in `as_x86_encode.c`, with different rules for the displacement's size; the x86-64 memory encoder is here three times (`:3800`, `3890`, `3980`), differing in a prefix and a bit; there are two sets of x87 tables (`:4131-4232`, `4373-4525`); three functions say how wide a register is and disagree (`parse_x86_reg_bits` 1144, `x86_reg_width_bits` 1355 — for which any name beginning with `e` is 32 bits, `:1398` — and `is_x86_low8_reg` 3786); three constant evaluators (`:604-857` on strings, `858-944` on trees, `eval_expr_asm_vars`).  Dead: the `movq` block (5981-5995), the `movdir64b`/`enqcmd` test inside the `movnti` branch (6346-6353), the empty `if` at 4961-4965.  What is wanted: one operand model with class and width; one ModRM/SIB/REX/prefix emitter that returns where each displacement and immediate is (AS-DES-001); one table from a hashed mnemonic to templates of operand classes, replacing both ladders, the 45 lookups and the per-extension chains; x87 and SIMD as table data in files of their own; `as_elf_emit.c` left with layout, sections, symbols and relocations.
  Basis: traced.

Right: the ModRM emitter for constant displacements, with `%esp` as
base, index alone, and the eight- and 32-bit thresholds; the SIMD opcode
tables for register and constant-memory forms, in which the sweep found
nothing wrong beyond the items above; moves to and from segment, control
and debug registers; `movabs`; `cvttsd2si`'s `REX.W`; `k{and,andn,or,xor,
xnor,add}{b,w,d,q}`; the fences, SHA, AES, `pclmulqdq`, `palignr`, VMX and
the newer single instructions.

## 6. Layout, sections, symbols and data

`as_symtab.c`, `as_sections.c`, `as_data.c`, `as_relax.c`.  Three facts
about them explain most of what follows:

- **`as_relax.c` is not called.**  Nothing calls `as_relax_branches`;
  branches are sized in `as_elf_emit.c`.  Its 387 lines model every
  instruction that is not a branch as four bytes and every directive as
  none (`:251-262`).  `ARCHITECTURE.md:83` describes it as the relaxation
  pass.
- **What `as_data.c` builds is thrown away.**  `emit_data_program` begins
  `(void)data` (`as_elf_emit.c:12313`).  The module is a second validator
  of the data directives, with rules of its own, and one function that is
  used (`as_decode_string_literal`).
- **Symbol and section directives are read by string splitters**
  (`as_symtab.c:272-521`, `as_sections.c:254-353`), not by the expression
  parser, so each has a grammar of its own.

Findings already given under the front end or the object writer —
`.p2align` with a maximum (AS-FE-002), undefined local labels
(AS-FE-003), `.comm` (AS-OBJ-005), directives that emit nothing
(AS-OBJ-008), relocation widths (AS-OBJ-006), default section flags and
code in sections not marked executable (AS-OBJ-009), `2^63` and above
(AS-OBJ-016) — were found again here independently and are not repeated.

### 6.1 Layout

- [ ] **AS-LAY-001** The distance between two places shall include the alignment padding between them.
  Evidence: `as_elf_emit.c:10897-10915` finds a directive's size by running it into an empty buffer, and `:11621` computes padding from that buffer's length, so every `.align`, `.balign` and `.p2align` has size 0 in the prediction; the memo at `:10811-10819` assumes size does not depend on offset.  **A branch over an alignment goes to the wrong place:** `jmp 1f` / `.skip 120,0x90` / `.p2align 4,0xcc` / `1: ret` is `eb 78`, a jump to 0x7a, into the `int3` fill; the `ret` is at 0x80 and GNU writes `eb 7e`.  Backward branches and 64-bit mode the same.  **A difference of labels taken from another section is wrong:** `a: .byte 1` / `.p2align 4` / `b: .byte 2`, then `.long b-a` in another section, is 1, not 16, though the symbol table has `b` at 0x10.
  Basis: reproduced (both re-run by hand).
- [ ] **AS-LAY-002** A branch across `.org` shall reach its label, and a difference across a relaxed branch shall use the branch's final size.
  Evidence: `jmp 1f` / `.skip 60` / `.org 0x90` / `1: ret` jumps to 0x93: the padding was computed with a two-byte jump that was then widened.  `a: jmp 9f` / `.skip 200` / `9: b: ret` then `.long b-a` elsewhere is 0xca for 0xcd; within the section `.long b-a` is 2 for 0xce, and `.skip b-a` emits 2 bytes for 206.
  Basis: reproduced.
- [ ] **AS-LAY-003** A local label shall be found in whatever section it is, and a branch to an absolute address shall be relocated.
  Evidence: `.data` / `1: .long 0` / `.text` / `jmp 1b` / `call 1b` / `movl $1b,%eax`: the `jmp` is not emitted, the `call` has no relocation, the immediate is 0.  `.long 1b, 2f` in data makes relocations against undefined globals named `1b` and `2f`.  `jmp 0x1234` is `e9 2f 12 00 00` with no relocation, right only if `.text` is loaded at 0.  A branch to a named label in its own section is never shortened and always carries a relocation (`jmp l` / `l: ret` is `e9` where it is `eb 00`).
  Basis: reproduced.

### 6.2 Sections

- [ ] **AS-SEC-001** `.pushsection name, n` shall not change the section's flags.
  Evidence: `as_sections.c:518-519` reads the second argument as a flags string — `"1"` is no flags — and `:469-472` overwrites those of a section that exists.  `.text` / `nop` / `.pushsection .text, 1` / `ret` / `.popsection` / `nop`: `.text` has no flags and size 0.  **All the code is gone, status 0.**
  Basis: reproduced (re-run by hand).
- [ ] **AS-SEC-002** Subsections shall be ordered by number and joined to their section.
  Evidence: `as_sections.c:616-630` makes a second section of the same name, which becomes a second, empty ELF `.text` (`as_elf_emit.c:14935`); content is in source order; `.text 1` and `.data 1` are ignored; an alignment in a subsection is recorded on the empty twin.
  Basis: reproduced.
- [ ] **AS-SEC-003** `.popsection` shall restore the previous section as well as the current.
  Evidence: `as_sections.c:598-607` saves the current index only; a `.previous` after a pop lands in the wrong section.  `.popsection` on an empty stack is an error where GNU warns.
  Basis: reproduced.
- [ ] **AS-SEC-004** Section groups shall be formed as written.
  Evidence: `as_sections.c:538-563` takes the fourth argument for the entry size and then compares the fifth with `comdat`: `"aMSG",@progbits,1,grp,comdat` loses its group.  Two sections naming one group are not both members, and the signature appears twice, once undefined, with the group's `sh_info` on the undefined one.
  Basis: reproduced.
- [ ] **AS-SEC-005** Section flags and types that are not known shall be refused; contradictions shall be reported.
  Evidence: `as_sections.c:387`, `:395-423` ignore them: `"aR"`, `"ae"`, `"aq"`, `@bogus`, `@init_array`, `@unwind`, a numeric type are all `PROGBITS`; `"0x3"` gives the `x` flag; `"aM"` with no entry size is accepted; `"axwo"` makes a group whose signature is `.text`.  A second `.section .foo` with other flags rewrites them (`:469-472`).  Data in `.bss` is accepted and dropped.
  Basis: reproduced.
- [ ] **AS-SEC-006** Alignment directives shall have one meaning and one set of limits.
  Evidence: three validators, `as_sections.c:656`, `as_data.c:595`, `as_elf_emit.c:11592`, with limits of 31, 63 and the word size.  `.align 0` is "malformed"; `.align A` with `A` set by `.set` is "malformed section directive"; `.balignw` and its fellows emit nothing.  Padding in code is single-byte NOPs.  `.text` is always aligned to 16 and `.data`, `.bss`, `.rodata` to 4, and an empty `.rodata` is always emitted (`as_sections.c:236-248`).
  Basis: reproduced.

### 6.3 Symbols

- [ ] **AS-SYM-001** `.set` with a constant expression shall define an absolute symbol.
  Evidence: `as_symtab.c:812-829` takes anything that is not a bare number for "symbol ± number".  `.set g1, 2*8`: the symbol table gets an undefined `g1` and an undefined global named `2*8`; `.long g1` is 16, the emitter having an evaluator of its own, but `movl $g1,%eax` is a relocation against `g1` with immediate 0.  `.set f1, 4+c1` and `.set j1, e1 - c1` are "malformed directive .set".
  Basis: reproduced (re-run by hand).
- [ ] **AS-SYM-002** An alias shall resolve whatever the order of definition, shall take its target's type and size, and a loop shall be reported.
  Evidence: `as_elf_emit.c:13004-13086` copies values in one pass in table order.  `.set a1, b1` / `.set b1, c1` / `c1: nop` leaves `a1` undefined.  `.set x, y` / `.set y, x` is accepted.  An alias of an undefined symbol relocates against the alias, a local undefined symbol, which no linker accepts.  `.set g, f` gives `g` neither `FUNC` nor size.
  Basis: reproduced.
- [ ] **AS-SYM-003** If `.size` is given an expression it does not understand, then it shall fail, and shall not compute something else.
  Evidence: `as_symtab.c:469-515`: after the string is cut at the `-`, a right-hand side that does not match falls into the "bare symbol" branch (`:506`).  With `f: nop; nop; e:`, `.size f, e-f+4` is 2, not 6; `.size f, e-g` is 3, not 2; `.size f, e` is 2 where GNU refuses.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SYM-004** `.type` shall know every type, and refuse what it does not.
  Evidence: `as_symtab.c:561-589` answers `NOTYPE` for a name it does not know.  `@gnu_indirect_function` is `NOTYPE`: an ifunc's resolver would be called as the function.  `@gnu_unique_object` likewise.
  Basis: reproduced (re-run by hand).
- [ ] **AS-SYM-005** Binding shall follow what was written.
  Evidence: a symbol named only by `.type`, `.size` or `.hidden` is left local and undefined, which is not valid ELF (`as_symtab.c:1014` promotes only what an instruction's operand referred to, `:1002-1010`); `.globl .Lg` is left out of the symbol table (`as_elf_emit.c:12836` skips every `.L`); `x = 6` / `x = x + 1` leaves `x` out (`:12839`).  A label then `.set` of the same name is accepted and the symbol becomes absolute (`as_symtab.c:802-810`); the other order, which GNU accepts, is refused; `.equiv` twice is accepted.
  Basis: reproduced.

### 6.4 Data

- [ ] **AS-DAT-001** If an operand of a data directive does not parse, then the assembler shall fail.
  Evidence: it becomes an undefined global symbol named after the text, with a relocation: `.long 1 2` (a symbol `12`), `.long 12abc`, `.byte 09` (a 32-bit relocation on one byte), `.long 0x`, `.long 5/0`, `.long 2**3`, `.long 1e2`, `.long !0`.  All status 0.
  Basis: reproduced.
- [ ] **AS-DAT-002** A count that is a constant shall be evaluated.
  Evidence: `.set e1,0` / `.set e2,3` / `.zero e2-e1` is "cannot evaluate .zero count expression"; `as_data.c:559-565` accepts any expression without evaluating it, and `as_elf_emit.c:11631-11633` sets the repeat to 1 when the text happens to contain `0b`, `-` and `.`.
  Basis: reproduced; the heuristic traced.
- [ ] **AS-DAT-003** Values out of range, and unusual spellings of numbers, shall be reported.
  Evidence: no warning for `.byte 256`, `.word 65536`, `.long 0x100000000`; `.float 0x3fc00000` is read as hexadecimal floating point; `0f1.5` is refused (`as_data.c:498`); `.incbin "f",470` past the end emits nothing; `\a`, `\xg`, `\8` differ from GNU (`as_data.c:82-151`).
  Basis: reproduced.

### 6.5 Robustness and time

- [ ] **AS-ROB-001** The splitters shall not recurse once for each operator.
  In part: the splitters are gone (AS-DES-005), and with them the negation at `as_symtab.c:359`.  Still open: the count before the allocation check in `as_symtab.c` and the unchecked multiplication in `as_sections.c`.
  Evidence: `parse_s64` (`as_data.c:189-320`) recurses for each `+` or `-` and copies the remainder at each level: stack linear, time quadratic.  `as_sections.c:254-353` has the same shape.  (The crashes are under AS-FE-021.)  `as_symtab.c:243-250` counts an entry before checking the allocation of its name; `as_sections.c:213` multiplies without an overflow check; `as_symtab.c:359` negates `LLONG_MIN`.
  Basis: reproduced; the last three traced.
- [ ] **AS-PERF-003** Assembly time shall not be cubic.
  Evidence: `dN: .long dN - dN-1` in data, 2,000 lines: 48 seconds; in 32-bit mode 250, 500 and 1,000 lines take 0.11, 0.59 and 3.9 seconds.  `1: nop` with a `.long 1b - .` pushed into another section, 2,000 / 4,000 / 8,000: 3.2 / 19 / 54 seconds.  `find_as_symbol_const` (`as_elf_emit.c:12260`) scans linearly past the hash index `as_symtab.c` keeps; `as_sections.c:182-206` scans by name for each directive.
  Basis: measured.

### 6.6 Design

- [ ] **AS-DES-005** There shall be one expression evaluator, and no module whose result is unused.
  In part: there is one.  `as_expr.c` has the lexer, the parser, `as_expr_eval` (a value, given a callback for what symbols are worth) and `as_expr_eval_linear` (a number plus one symbol minus another).  Gone in its favour: the parser in `as_parser.c`; `const_expr_parse_*`, the bodies of `eval_expr_const` and `eval_expr_asm_vars`, `parse_symbol_addend_arg`'s splitter and `expr_symbol_addend`/`_with_local` in `as_elf_emit.c`; `parse_s64`/`parse_u64` in `as_data.c`; `parse_u32_arg` in `as_sections.c`; `parse_i64_arg`, `parse_set_rhs_symbol`, `parse_set_rhs_dot_minus_symbol` and the body of `parse_size_arg` in `as_symtab.c`; `eval_rept_count` and the number-reading of `.if` and `.ifeq` in `as.c`.  A case table of 296 directive and operand expressions matched GNU's bytes in 223 before and 259 after, none worse; the audit's 29,778 instruction lines and the old suite's sources assemble to the same objects.  What remains of this requirement is not evaluation: `eval_local_rel_expr_virtual` and its five fellows (`as_elf_emit.c`) walk the tree to place symbols by a predicted layout, and go with AS-DES-002; `.if` compares as text a condition with a name in it, there being no symbols in that pass (AS-FE-050); the copied helpers, the two section state machines, `as_relax.c` and `as_data.c`'s unused program are as they were.
  Evidence: four evaluators with four grammars: `as_symtab.c:272-521`, `as_sections.c:254-353`, `as_data.c:189-349`, `as_elf_emit.c:584/843` — besides the parser's (AS-FE-006) and the three in the emitter's first half (AS-DES-004).  `trim_copy`, `xstrdup` and `set_err` are copied into each file, and `as_sections.c:10-60` defines the ELF constants again.  Two section state machines (`as_sections.c` and `section_track_*`, `as_elf_emit.c:7994`) must agree for an alignment to land on the right section.  `handle_directive` is `as_symtab.c:591-869`.  `as_relax.c` and the program `as_data.c` builds should be deleted or made the one source of truth.
  Basis: traced.

Right: binding, visibility and section for defined symbols; `.size f,
.-f` and the plain forms; `.set` to a number or `sym±const`; duplicate
labels refused; the short/near boundary for branches to numeric labels
at 126-129 forward and 125-128 back; label differences within a section,
across `.p2align`, `.org`, `.fill`, strings and floats; `"aMS"` entry
sizes; a COMDAT of one section; nested `.pushsection`; the fill byte of
`.align N,fill`; `.float` and `.double` of ordinary values.

## 7. The core x86 encoder (`as_x86_encode.c`, `as_x86_reloc.c`)

Audited differentially: each line of a generated corpus assembled as a
file of its own by this assembler and by GNU `as` 2.47, comparing the
bytes, the relocations and the disassembly.

| Class | `--32` (11,706 lines) | `--64` (18,072 lines) |
|---|---|---|
| identical | 6,600 | 6,905 |
| same meaning and length, other bytes | 234 | 703 |
| bytes differ | 1,603 | 2,874 |
| relocation differs only | 129 | 196 |
| ours longer, same meaning | 1,284 | 1,020 |
| GNU refuses, ours accepts | 427 | 943 |
| GNU accepts, ours refuses | 572 | 3,924 |
| both refuse | 857 | 1,507 |

Identical output for 56 % of the lines in 32-bit mode and 38 % in 64-bit.

### 7.1 Structure

- [ ] **AS-X86-001** When a branch of the 64-bit encoder has encoded an instruction, the encoder shall return it.
  Evidence: `as_x86_encode.c:5953-5957`.  The `psrlq`/`psllq` branch ends `goto finish; } /* Fall through … */ }` and line 5957 begins a new `if`, not an `else if`.  The first chain (3415-5956) has that one `goto finish`; everything else it encodes falls into the second chain, whose final `else` (6445-6446) says "unsupported x86_64 mnemonic".  **Refused in 64-bit mode, all of them encoded correctly a moment before:** every form of `test` (829 corpus lines; `testl %eax,%ebx`, `testq %rdi,%rdi`), `adc` and `sbb` between registers, `rol`/`ror`/`rcl`/`rcr`, one-operand shifts, `neg`, `not`, three-operand `imul`, `xchg`, `xadd`, `cmpxchg`, `bt*`, `bsf`/`bsr`, `in`/`out`, `lgdt`/`lidt`/`invlpg`, `nopl`/`nopw`, `cpuid`, `rdtsc`, `fnstsw`, every x87 `%st(i)` form, `movd`, `sqrtsd`.  The stray brace came in with `51195cbda` (2026-04-30); the sources restored in `3169d142b` are byte for byte those deleted, so this is not new, and it is the largest single reason the old tests fail (AS-TST-002).
  Basis: reproduced (re-run by hand; the history checked).
- [ ] **AS-X86-002** `as_x86_reloc.c` shall be used, or removed.
  Evidence: nothing calls `as_x86_emit_reloc` or `as_x86_reloc_type`.  The live code is the `strstr` on symbol names of AS-OBJ-003.  The table that is not used is itself short of `R_386_16`/`8`/`PC16`/`PC8`, the i386 TLS local-exec and initial-exec types, and `R_X86_64_8`/`16`/`PC8`/`PC16`/`DTPOFF32`.
  Basis: reproduced.
- [ ] **AS-X86-003** One mnemonic shall be handled in one place.
  Evidence: i386: `clc`…`std` at `as_x86_encode.c:1872` and 2291; `wrmsr`/`rdtsc`/`rdmsr`/`sysenter` at 1938 and 2485; `invd` at 2461 and 2481.  x86-64: `mov` at 3179-3297 and 3742; `add` and its fellows at 2748, 3098, 3132, 3961; `movzx` 2896/3848; `imul` 3001/3862; `push` 3020/5556; `pop` 3053/5582; `mul`/`div` 2699/3904; `lar` 2796/4282; the string instructions 2662/5073.  Branches made unreachable because `normalize_x86_mnemonic` has already taken the suffix off (`as_elf_emit.c:3381`): `movsb`/`movsd`/`stosb` (1568 on), `incb`/`incl`, `mulb`/`mull`, `movsxw`/`movzxw` (1472, 1483).
  Basis: traced.

### 7.2 Operand size

- [ ] **AS-X86-010** The operand size of an instruction without a suffix shall come from its registers, and a mismatch shall be refused.
  Evidence: `as_elf_emit.c:9237` infers the width only in Intel syntax; `as_x86_reg_t` has no width.  **Silently the 32-bit instruction:** `mov %al,%cl` is `8b c8` (`movl %eax,%ecx`), where it is `88 c1`; `mov %ax,%bx` is `8b d8`; `add %cl,(%eax)` is `01 08`; `inc %al` is `40` (`incl %eax`); `shl $1,%cl`; `in $0x60,%al` is `e5 60`.  The same for `neg`, `not`, `mul`, `div`, `push`, `pop`, `xchg`, `bt`, `bsf`, `cmov`, `imul` with 8- or 16-bit registers.  **Accepted though invalid:** `movl %ax,%ebx`, `mov %al,%ebx`, `addw %al,%bx`, `movsbl %ax,%ebx`, `seta %ax`, `lea (%eax),%al`, `bswap %ax`, `jmp *%al`, `pushb %al`, `lgdt %eax`, `hlt %eax`, `nop $1`, `mov $1,%es`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-X86-011** A 16-bit form shall carry `66`, once.
  Evidence: `cbw`/`cbtw` are `98` and `cwd`/`cwtd` `99` in both modes (`as_x86_encode.c:2085-2093`, `6317-6328`); `iretw` (2167), `lretw` (2175), `pushaw`/`popaw` (2001-2005), `jmpw *%ax`, `callw *%ax`; `cmovow`, `leaw`, `xaddw`, whose suffix is not taken off; `data16 movl %eax,%ebx`.  In 64-bit mode every `w` instruction gets it **twice**, `66 66` (`emit_prefixes`, `:787`, and the fast paths at 3230, 3263, 3277, 3291 each write one); `movabs %ax,sym` gets none.
  Basis: reproduced.
- [ ] **AS-X86-012** An immediate shall be the size of its operand.
  Evidence: in 64-bit mode `movb $1,%al` is `b0 01 00 00 00` — `as_x86_encode.c:3257-3258` asks for 8 bits of a helper (`:695-700`) that writes 32 for anything but 16 — and the three extra bytes execute as `add %al,(%rax)`.  `addw $0x1000,%bx` is `66 81 c3 00 10 00 00` (`:2789-2790` uses `emit32`), `cmpw $imm,(%rax)`, `andw`, `pushw $imm` (`:3046`) the same: two bytes too many, executed.
  Basis: reproduced (both re-run by hand).
- [ ] **AS-X86-013** If a value does not fit its field, then the assembler shall refuse it.
  Evidence: casts to `uint8_t` and `uint32_t` throughout (`as_x86_encode.c:1287`, `1309`, `1422`, `2789`, `3288`).  `addq $0xffffffff,%rax` is `48 81 c0 ff ff ff ff`, which adds -1; `addq $0x80000000,%rbx` and `pushq $0x80000000` have their sign turned; `movq $0x123456789,(%rax)` loses its top; `int $256` is `cd 00`; `enter $0x10000,$0`, `shl $-1`, `bt $256` are accepted.
  Basis: reproduced (re-run by hand).

### 7.3 Wrong encodings

- [ ] **AS-X86-020** `adcb` and `sbbb` shall be `adc` and `sbb`.
  Evidence: the byte-operation ladder at `as_x86_encode.c:1381-1406` has no arm for either and falls to `cmp`.  `adcb %al,%al` is `38 c0`, `cmp %al,%al`, where it is `10 c0`; `sbbb $1,(%eax)` is `80 /7`; `lock sbbb %cl,(%ebx)`.  **Multi-byte arithmetic written with byte carries compares instead of adding.**
  Basis: reproduced (re-run by hand).
- [ ] **AS-X86-021** `ret $n` shall keep its count.
  Evidence: `as_x86_encode.c:2189-2192` emits `c3` whatever the operand.  `ret $8` is `c3`; `retw $8` is `66 c3`.  (Seen from the dispatch as AS-OBJ-013.)
  Basis: reproduced.
- [ ] **AS-X86-022** The high byte registers shall be themselves; the new low byte registers shall have their `REX`.
  Evidence: `modrm_sib_disp` uses `reg_field & 7` for `reg_code3()` at `as_x86_encode.c:845`, `897`, `920`, `934`, `953`: with an absolute or index-only operand `%ah`, `%ch`, `%dh`, `%bh` become `%al`, `%cl`, `%dl`, `%bl` — `addb %bh,sym` is `00 1d`, where it is `00 3d`.  In 64-bit mode the `mov`, ALU and `inc`/`dec` fast paths (`:3267-3297`) never set `force_rex`: `movb %sil,%dil` is `8a fe`, which is `mov %dh,%bh`; `movb %al,%sil`, `cmpb %dil,%sil`, `incb %dil` likewise.  And `%ah` with a `REX` is accepted: `movb %ah,%r8b` is `44 8a c4`, which is `%spl`.
  Basis: reproduced (re-run by hand).
- [ ] **AS-X86-023** `enter`, `xchgb`, `jcxz` and the `movsx`/`movzx` aliases shall be encoded as written.
  Evidence: `enter $8,$1` is `c8 01 00 08`, its operands exchanged (`as_x86_encode.c:1862-1866` and `6168` expect the size first; `as_elf_emit.c:9225` has reversed them), in both modes.  `xchgb %al,%bl` is `87 c3`, the 32-bit exchange (`:1322-1326`), and `xchgl (%ebx),%ebx` is refused.  `jcxz` has no `67` (`:2193-2231`): it tests `%ecx`.  `movsxw %ax,%ebx` is `66 0f be d8`, `movsbw %al,%bx` (`:1471-1492`); `movsx %al,%bx` has no `66`; in 64-bit mode `movsxl %eax,%rbx` is `movsbq`.
  Basis: reproduced (`enter`, `xchgb`, `jcxz` re-run by hand).
- [ ] **AS-X86-024** `%r12` shall be usable as an index, and prefixes shall be in the order the processor reads them.
  Evidence: `as_x86_encode.c:1076` tests `(index & 7) == RSP`, which is true of `%r12`: `movl %eax,(%rax,%r12)` is "RSP cannot be used as SIB index" (370 corpus lines; 43 lines of gcc's own output).  `push %r9w` is `41 66 51`: `REX` before `66` is ignored, so it pushes `%cx` (`:3022`).  `movq %xmm0,%rax` is `48 66 0f 7e c0`, which decodes as `movd %xmm0,%eax` (`:3179-3198`).  `cvtsi2sd %eax,%xmm0` has `REX.W` (`:4577`, `4651`).
  Basis: reproduced (`%r12` re-run by hand).
- [ ] **AS-X86-025** String instructions shall take the overrides written and no others.
  Evidence: `movsl (%esi),%es:(%edi)` is `26 a5`: the destination's `%es`, which is not optional, is taken for an override of the source (`as_elf_emit.c:9429-9436`); `movsw %cs:(%esi),%es:(%edi)` is `66 26 66 a5`, the `%cs` lost and the `66` doubled (`as_x86_encode.c:1561`, `1608`); `stosl %eax,%ds:(%edi)` is accepted.
  Basis: reproduced.
- [ ] **AS-X86-026** Single mistakes.
  Evidence: `ud2b` is `0f 0b` for `0f b9` (`as_x86_encode.c:2559`); `rolw %cl` is rotate by one, `%cl` taken for the operand (`:1530`); `lock` is accepted on a register destination and on loads (`mnemonic_lock_compatible`, `:723`, looks at the mnemonic only); `testb $1,$2` is accepted on purpose (`:2072-2084`); `je 0x1000` has no relocation.
  Basis: reproduced.

### 7.4 Modes

- [ ] **AS-X86-030** `.code16`, `.code32` and `.code64` shall select the mode of what follows, and each mode shall refuse what belongs to another.
  Evidence: under `--64` the directives are ignored (`as_elf_emit.c:9179` forces 64): `.code16` then `movl %eax,%ebx` is `8b d8`.  `.code64` under `--32` is accepted and `movq %rax,%rbx` is `8b d8`.  `.code16gcc` behaves as 32-bit.  In `.code16`: `movw sym,%ax`, `lgdt sym`, `callw sym` are four zero bytes with a 32-bit relocation at offset 0; `je sym` is the six-byte form; `push %eax`, `xor %eax,%eax` lack `66`.  **A boot sector or real-mode stub cannot be assembled.**  In 32-bit mode `pushq %rax`, `incq`, `retq`, `leaveq`, `callq`, `jmpq *%eax`, `stosq`, `rex movb`, `in $0x60,%rax` are accepted; in 64-bit mode `push %eax`, `pop %ebx`, `into`, `salc`, `pushfl`, `retl`, `jmp *%eax`, `calll *(%rax)` are (`:6223`, `6227`).
  Basis: reproduced (`push %eax` in 64-bit mode re-run by hand).

### 7.5 Crashes and gaps

- [ ] **AS-X86-040** The encoder shall count its operands before it reads them.
  Evidence: `as_x86_encode.c:1975` reads `a->u.reg` before the count is checked at 1978: `inc` with no operand is a null dereference under AddressSanitizer.
  Basis: reproduced.
- [ ] **AS-X86-041** The ordinary forms shall be accepted, and a refusal shall have a message.
  Evidence: bare `movsl`, `movsb`, `rep movsl`, `insb`, `outsl` are "unsupported operand form for 'movs'" (`as_x86_encode.c:1548`, `1687`, `1742` want operands; the arms for the suffixed names are dead); `xlat` (`:2171`), `aad`, `aam` (`:1916-1925`) and `loop`/`jecxz` to a local label fail with an **empty** message; `testl (%ebx),%eax` (`:2055`); `imul $imm,%reg`; `sal*`; `iretl`; `%cr8`; a prefix on a line of its own; `notrack`, `endbr32`/`endbr64`, `xacquire`; `vmcall`; `xsetbv`; `repz ret`; in 64-bit mode `mov sym,%es`.
  Basis: reproduced.
- [ ] **AS-X86-042** The shortest encoding shall be chosen where the choice is free.
  Evidence: no sign-extended `imm8` form in the i386 arithmetic group (`as_x86_encode.c:1416-1424`): `addl $4,(%eax)` is six bytes for three.  No `6a` push (`:2268`); no accumulator short forms; `(%ebp)`, `(%rbp)`, `(%r13)` take a 32-bit displacement (`:962-968`, `1125-1131`); `shl $1` is the `c1` form; `pushq %rbp` is `48 55`.  1,284 and 1,020 corpus lines are longer than GNU's.  Sizes of functions differ from the reference throughout.
  Basis: reproduced.

Right: ModRM and SIB for every base, index and scale but those above,
with `%esp`, `%rsp` and `%r12` as base, the 16-bit addressing forms, and
scale 3 and `%esp` as index refused; `REX.R/X/B` in the paths that
survive; `movq $imm,%r64` choosing between the sign-extended and the full
form; `movabs`; `movslq` and the suffixed `movz`/`movs` names; `cltq`,
`cqto`; rip-relative operands with no immediate after them; `@GOTPCREL`,
`@PLT`, `@GOTTPOFF`; i386 `movl $sym`, `pushl $sym`, `call sym@PLT`;
moves to and from control, debug and segment registers; `setcc`,
`cmovcc` and `jcc` for all thirty condition names; the short/near choice
for numeric labels; x87 memory forms with explicit suffixes; `lret $n`,
`syscall`, `swapgs`, `iretq`.

Not reached: Intel syntax; a full reading of the SSE and MMX branches
inside `as_x86_encode.c` (4290-6110).

## 8. The x86 extensions: SSE to AVX-512

The eighteen extension files, and wherever MMX, SSE and SSE2 come from.
Audited differentially over the cross-product of some 1,050 mnemonics
and 500 operand shapes.

| mode | GNU-valid lines | same | longer | EVEX for VEX | **wrong** | refused | invalid accepted (sampled) |
|---|---|---|---|---|---|---|---|
| `--64 -march=x86-64-v4` | 33,339 | 10,487 | 1,111 | 3,050 | 7,565 | 11,114 | 1,450 |
| `--32` | 20,866 | 8,471 | 1,296 | 1,844 | 5,274 | 3,981 | 1,510 |
| `--64 -march=x86-64-v3` (VEX families) | 18,802 | 6,438 | 475 | 128 | 2,723 | 9,038 | 207 |

Of 51,959 valid lines in the first two modes, 15,562 assemble to bytes
that disassemble as something else and 13,019 are refused.  (Some 1,276
of the 32-bit "wrong" are only a byte longer: a bare address with a SIB.)

- [ ] **AS-EXT-001** SSE and SSE2 shall be available in 64-bit mode.
  Evidence: the severed chain of AS-X86-001 (`as_x86_encode.c:5956-5957`).  `xorps %xmm0,%xmm0` is "unsupported x86_64 mnemonic: xorps".  So `addps`/`pd`, `mulps`/`pd`, `andps`/`pd`, `sqrtps`/`sd`, `movhlps`, `movhps`, `movd`, `cvtsd2si`, `cvtps2pd`, the `punpck*`, `pcmpeq*`, `pack*` and `pshuf*` families, `shufps`, `movmskps`, `ldmxcsr`: 51 SSE and 45 SSE2 mnemonics refused outright though the code for them is there (`xorps` at 4990).  **The baseline floating-point instruction set of x86-64 does not assemble.**
  Basis: reproduced (re-run by hand).
- [ ] **AS-EXT-002** An AVX instruction on `xmm` or `ymm` registers shall be VEX-encoded.
  Evidence: `as_elf_emit.c:9328-9351` tries the AVX-512 encoders first, in 32-bit mode always and in 64-bit at `-march=x86-64-v4`.  `vaddps %xmm1,%xmm2,%xmm3` under `--32` is `62 f1 6c 08 58 d9`, where it is `c5 e8 58 d9`: it needs AVX-512VL and is an invalid opcode on a processor with AVX2.  1,844 lines in 32-bit mode, 3,050 at v4.  And the compare rows of those tables do not check their destination (`as_x86_avx512bw.c:223` and the like): `vpcmpeqb %xmm1,%xmm2,%xmm3` is encoded with destination `%k3`; `vcmpps`, `vpcmpgt*` the same.
  Basis: reproduced (re-run by hand).
- [ ] **AS-EXT-003** Every extension file shall be reachable, and plain `--64` shall assemble what the compiler emits for the options it was given.
  Evidence: nothing calls `as_x86_encode_f16c` or the entry of `as_x86_avx512cd.c`, though both are built (`Makefile:6`): `vcvtph2ps`, `vcvtps2ph`, `vpconflictd`, `vplzcntq` are refused in every mode.  Plain `--64` refuses all AVX and BMI unless `-march=x86-64-v3` is given, which GNU does not ask.
  Basis: reproduced (re-run by hand).
- [ ] **AS-EXT-004** The extension encoders shall honour symbolic displacements, segment overrides and the mode.
  Evidence: nine private copies of `modrm_sib_disp64` (`as_x86_vex.c:65`, `as_x86_evex.c:73`, `as_x86_sse3.c:90`, and in ssse3, sse41, sse42, v2, v3_misc, bmi1), none of which has the core encoder's `force_disp32`, segment, address-size or mode.  `haddps foo(%rdx),%xmm2` is `f2 00 00 00 00` (AS-OBJ-002 again).  `pandn foo,%xmm2` in 64-bit mode is `66` and eight zero bytes with an `R_X86_64_64`.  **`pshufb %fs:(%rax),%xmm2` is `66 0f 38 00 10`: the segment prefix is gone**, for every instruction from SSE3 to AVX-512 and BMI — a thread-local access reads another address; 909 lines in 32-bit mode with `%gs:`.  In 32-bit mode `addps %xmm9,%xmm1` is `0f 58 c9`, the register masked to `%xmm1`; `haddps (%rax),%xmm1`, `andn %rax,%rbx,%rcx`, `pextrq $1,%xmm1,%rax` are accepted (`as_elf_emit.c:8391-8393` passes `is64 = 0` and nothing checks).
  Basis: reproduced (the segment and `%xmm9` re-run by hand).
- [ ] **AS-EXT-005** The `66` prefix shall be before `REX`, and shall be there only for the XMM form.
  Evidence: `as_x86_encode.c:6099` (the group 6036-6104) writes `66` after the place kept for `REX` (`:2651`): `pxor %xmm8,%xmm8` is `45 66 0f ef c0`, which is `pxor %xmm0,%xmm0`.  43 mnemonics — `padd*`, `psub*`, `pand`, `por`, `pxor`, `pmul*`, the shifts, `pavg`, `pmin`/`pmax`, `movntdq`.  And `as_x86_ssse3.c:194` writes `66` always: `pabsb %mm1,%mm2` is the XMM instruction; all sixteen SSSE3 mnemonics with MMX operands, and MMX `pextrw`.
  Basis: reproduced (both re-run by hand).
- [ ] **AS-EXT-006** An EVEX prefix shall be built as the manual gives it.
  Evidence: `as_x86_evex.c:220` has `R` and `R'` exchanged: `vpord %ymm1,%ymm2,%ymm11` is `62 e1 …`, destination `%ymm19`, where it is `62 71 …`.  Every EVEX instruction whose destination is register 8 to 15.  `as_x86_evex.c:133`, `160` write the displacement as an eight-bit one **uncompressed**: `vandpd 8(%rsp),%xmm2,%xmm3` reads `0x80(%rsp)` — the `disp8` of EVEX is scaled by the vector's size; 1,435 lines in 32-bit mode, and with AS-EXT-002 this is ordinary stack-relative AVX code.  Registers 16 to 31 are refused (`as_elf_emit.c:3644`, `3672`).
  Basis: reproduced (both re-run by hand).
- [ ] **AS-EXT-007** Masks, zeroing and broadcasts shall take effect or be refused.
  Evidence: for a compare into `%k`, the destination is used as the write mask (`as_elf_emit.c:8376`, `8419`, `8448`): `vptestmw (%rax),%zmm2,%k1` is encoded `{%k1}`.  The VEX and legacy paths never look at the decorators (`try_encode_x86_avx_stmt`, `:8079`, and its fellows): `vfmadd132ss %xmm1,%xmm2,%xmm3{%k1}` is the VEX instruction, unmasked; `addss (%rax){1to8},%xmm2` assembles.  `{z}` with no mask, two masks, `{1to3}` are accepted; `{1to8}` on a 16-element operand encodes as `{1to16}`.  Embedded rounding (`{rn-sae}`) is "invalid operand".
  Basis: reproduced.
- [ ] **AS-EXT-008** The tables shall have the right rows.
  Evidence: `imm8_with_implicit_reg` (`as_x86_avx.c:63-65`, `as_x86_fma.c:28-30`) computes `((reg & 7) | 8) << 4`: `vblendvps %ymm4,%ymm3,%ymm2,%ymm1` ends `c0` for `40`, naming `%ymm12`; `vblendvpd`, `vpblendvb` and all twenty FMA4 mnemonics.  `vpermilps`/`pd` in its variable form is listed under map `0F3A` for `0F38` (`as_x86_avx.c:243-244`), and so shadows the immediate form.  `:340`, `:355` pass `W = 0` for every promoted row: `vcvtsi2sdq %rax,%xmm1,%xmm2` converts `%eax`; `vpinsrq` encodes `vpinsrd`.  `vprol`/`vpror` put the destination where the opcode extension goes (`as_x86_avx512dq.c:155-160`): `vprord $3,%zmm1,%zmm2{%k1}` is `vpsrld $3,%zmm1,%zmm0{%k1}`.  The vector length is taken from the wrong operand (`:122`): `vcvtps2qq %ymm1,%zmm2`, `vinsertf32x8`.  `vpbroadcastb %eax,%ymm1` takes the general register for `%xmm0` (`as_x86_avx2.c:207-229`).  `crc32b mem,%ebx` is `crc32l`, and `crc32 %sil` lacks its `REX` (`as_x86_encode.c:239-250`, `as_x86_sse42.c:272`).  The opmask faults of AS-SEL-008 are in `as_x86_avx512f.c:361`, `437-442` as well.
  Basis: reproduced (`vblendvps` and `vcvtsi2sdq` re-run by hand).
- [ ] **AS-EXT-009** The instructions of each extension shall be encodable.
  Evidence: no row at all for 88 AVX mnemonics, among them **`vmovaps`, `vmovups`, `vmovdqa`, `vmovdqu`, `vmovd`, `vmovq`** — the moves — and `vsqrtps`, `vcvt*`, `vucomiss`, `vpshufd`, `vpmovsx*`/`zx*`, `vptest`, `vpextr*`, `vroundps`, the immediate shifts.  Encoders that exist and cannot be reached: `vextractf128`/`vextracti128` (`as_x86_avx.c:417`, `as_x86_avx2.c:163` want two operands; three are passed), `vpermq` (`:262`), the gathers (`:313-357`; a vector index is "mixed x86 memory addressing widths"), the `k` instructions' tables (`as_x86_avx512f.c:354-430`).  None anywhere for AES-NI, PCLMULQDQ and SHA in 64-bit mode.  `rorx` fails with an empty message.
  Basis: reproduced (`vmovaps` re-run by hand).
- [ ] **AS-EXT-010** Operands shall be of the class and number the instruction takes.
  Evidence: every extension encoder tests only that an operand is a register and takes its number `& 7`; extra operands are dropped (`as_x86_encode.c:161`, `184`, `209`, `232`).  `addsubps %rax,%rbx,%rcx`, `haddps $1,%xmm1,%zmm2,%zmm3`, `blendvps %r12,%rsp,%rbp`, `vxorps %k5,%k6,%k7`, `vfmsub231pd %eax,%ebx,%ecx`, `shlx %eax,%xmm1,%xmm2`, `lahf -0x100(%rbx),%ebx,%ecx` (which is `9f`) all assemble.
  Basis: reproduced.
- [ ] **AS-DES-006** An instruction shall be found by its name, not by trying encoders in turn.
  Evidence: dispatch is "call each encoder and take the first that returns 0", and `x86_stmt_requires_v3`/`_v4` (`as_elf_emit.c:9114-9153`) run every encoder again into a scratch buffer for each statement just to word an error.  The order of the attempts decides the encoding (AS-EXT-002).  One encoder's message is overwritten by the next's.  `modrm_sib_disp64`, `emit8`/`emit32`, `streq_ci`, `reg_low3`/`reg_ext`, `scale_bits`, `is_disp8` are copied into nine files, `encode_vex_with_optional_imm` into four.
  Basis: traced.

Right: `as_x86_vex.c`'s choice between the two- and three-byte prefix
and its `R`/`X`/`B`, `vvvv`, `L` and `pp` for registers 0 to 15; all
sixty FMA3 mnemonics in register and memory forms; BMI1 and BMI2 in both
widths; the three-operand AVX tables but for the rows above; the SSE3,
SSSE3 (XMM), SSE4.1 and SSE4.2 tables; EVEX for destinations 0 to 7 with
no displacement or a 32-bit one; `vzeroupper`.  10,500 lines through the
sanitizer build reported nothing.
