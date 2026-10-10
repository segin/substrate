# In-tree assembler (`usr.bin/as`) — task list

The work that `docs/as-audit.md` (2026-10-09) calls for, as one list to be
executed from the top.  Each task is a single requirement in EARS form
(INCOSE *Guide to Writing Requirements*): **ubiquitous** ("The assembler
shall …"), **event-driven** ("When …"), **state-driven** ("While …"),
**unwanted behaviour** ("If …, then …") or **optional feature**
("Where …").  One task states one thing, names no implementation, and can
be shown met or not met.

Each task carries:

- **Trace** — the audit entry it comes from; the evidence, the line numbers
  and the reproductions are there and are not repeated here.
- **Verify** — the method (**T** test, **A** analysis, **I** inspection,
  **D** demonstration) and what passes.  "GNU" means the bytes and
  relocations GNU `as` gives for the same source.  Unless it says
  otherwise a T is a case added under `tests/usr.bin/as/`.
- **After** — tasks that must be done first, where there are any.

How to work it (the repository's rule for task lists): one box at a time;
the change, its test and any man page or document it touches in one
commit; tick the box in that commit; push.  When the last task tracing to
an audit entry is ticked, tick the entry in `docs/as-audit.md` and give it
its "Met:" line.  A task found to be wrong, or already met, is ticked with
a note saying so, not deleted.

The order is by dependency, then by harm.  Section A makes the work
checkable.  Section B is what can be mended where it stands and does the
most damage today.  Sections C to G replace the six structural causes the
audit names, each followed by the defects that go with it.  Sections H to
L are the rest.

Numbering is stable: a task added later takes the next free number and is
placed where it belongs.

## 0. Done before this list was written

- [x] **AS-T-001** The assembler shall parse and evaluate expressions with one lexer, one parser and one evaluator.
  Trace: AS-DES-005 (evaluators), AS-FE-006 (grammar), AS-ROB-001 (the splitters).  Verify: T — `test_expr.sh`, `test_exprsites.sh`.  Commits `351c1ed6b` to `5ed3ac39d`.
- [x] **AS-T-002** If an expression exceeds 4096 nodes or 256 levels of nesting, then the assembler shall refuse it with an error.
  Trace: AS-FE-021.  Verify: T — `test_expr.c` (`check_limits`), `test_exprsites.sh`.
- [x] **AS-T-003** The assembler shall evaluate constant arithmetic in 64-bit two's-complement arithmetic without a trap.
  Trace: AS-SEL-013.  Verify: T — `test_expr.c`.
- [x] **AS-T-004** When `.set` is given an expression of numbers alone, the assembler shall define the symbol absolute with that value.
  Trace: AS-SYM-001 (first half).  Verify: T — `test_exprsites.sh`.
- [x] **AS-T-005** If an operand of a data directive is not an expression, then the assembler shall fail.
  Trace: AS-DAT-001.  Verify: T — `test_exprsites.sh` (`.long undefined_thing(`); `.long 1 2`, `12abc`, `09`, `0x`, `2**3`, `1e2` checked by hand 2026-10-09.
- [x] **AS-T-006** When `.rept` is given more than one expression, the assembler shall refuse it.
  Trace: AS-FE-012 (first item).  Verify: T — `test_exprsites.sh`.
- [x] **AS-T-007** When `.if` or `.ifeq` and its fellows are given an expression of numbers, the assembler shall decide by its value.
  Trace: AS-FE-007 (arithmetic).  Verify: T — `test_exprsites.sh`.
- [x] **AS-T-008** When `.size` is given `end - sym + n`, the assembler shall record the difference plus n.
  Trace: AS-SYM-003 (first item).  Verify: T — `test_exprsites.sh`.
- [x] **AS-T-009** When `.quad` is given a value of 2^63 or more, the assembler shall emit its 64 bits.
  Trace: AS-OBJ-016 (first item).  Verify: T — `test_expr.c`.

## A. Make the work checkable

- [ ] **AS-T-010** The repository shall hold, under `tests/usr.bin/as/`, the 147 files of the assembler's test suite as they were at `f2e3fe0d4^`.
  Trace: AS-TST-001.  Verify: I — `git diff --stat f2e3fe0d4^ HEAD -- tests/usr.bin/as` shows no file of the old suite missing.
- [ ] **AS-T-011** When `make -C tests/usr.bin/as` is run on a host with no cross toolchain, every test of the restored suite shall build and run.
  Trace: AS-TST-001.  Verify: D — the command completes and prints a verdict for each test.  After: 010.
- [ ] **AS-T-012** `tests/usr.bin/as/STATUS.md` shall record, for each restored test that fails, whether the cause is a defect (and the task here that mends it) or a stale expectation.
  Trace: AS-TST-002.  Verify: I — no failing test is without an entry.  After: 011.
- [ ] **AS-T-013** If a restored test fails because its expectation is stale, then the expectation shall be corrected against GNU `as`.
  Trace: AS-TST-002.  Verify: T — the test passes; the commit names the GNU version used.  After: 012.
- [ ] **AS-T-014** The test suite shall hold a differential harness that assembles each line of a corpus with this assembler and with GNU `as` and reports every difference in section bytes or relocations.
  Trace: AS-TST-001.  Verify: D — run over the audit's corpora (kept in the job scratch, `aud/x86/c32.txt`, `c64.txt`, `aud/ext/`) it reproduces the audit's counts within 1 %.
- [ ] **AS-T-015** Where GNU `as` is not installed, the differential harness shall report that it was skipped and shall not fail.
  Trace: AS-TST-001.  Verify: T — run with `PATH` emptied of `as`.  After: 014.
- [ ] **AS-T-016** The differential harness shall keep a baseline of known differences and shall fail when a line not in the baseline differs.
  Trace: AS-TST-001.  Verify: T — a deliberately broken encoder makes it fail; a mended line is removed from the baseline by the commit that mends it.  After: 014.
- [ ] **AS-T-017** The test suite shall run its corpora through a build of the assembler made with AddressSanitizer and UndefinedBehaviorSanitizer.
  Trace: AS-TST-001.  Verify: D — the run reports the null dereference of AS-X86-040 until 022 is done.
- [ ] **AS-T-018** The `ci` workflow shall run the whole of `tests/usr.bin/as` on every push.
  Trace: AS-TST-001.  Verify: I — `gh run view` shows the restored tests in the job log.  After: 011.
- [ ] **AS-T-019** The test suite shall compile a fixed set of C programs with `gcc -S` at `-O0` and `-O2`, for `-m32` and `-m64`, assemble the output with this assembler, link it and run it.
  Trace: AS-TST-001, summary ("does not assemble what a compiler writes").  Verify: D — the fifteen combinations of the audit are listed with a verdict each; the test is expected-fail per combination until it passes.

## B. Mend in place: silent wrong output and crashes

Nothing here waits on a redesign.  Each is a fault a user meets with exit
status 0.

### B.1 One-line causes with wide effect

- [ ] **AS-T-020** When a branch of the 64-bit encoder's first mnemonic chain has encoded an instruction, the encoder shall return that encoding.
  Trace: AS-X86-001, AS-EXT-001.  Verify: T — under `--64`, `testl %eax,%ebx`, `xchg`, `cmpxchg`, `cpuid`, `rdtsc`, `rol`, `neg`, `not`, three-operand `imul`, `bsf`, `xorps %xmm0,%xmm0`, `addpd`, `movd`, `sqrtsd`, `fnstsw` match GNU.
- [ ] **AS-T-021** The 51 SSE and 45 SSE2 mnemonics refused in 64-bit mode shall each assemble to GNU's bytes for register and constant-memory operands.
  Trace: AS-EXT-001.  Verify: T — the `sse` and `sse2` families of the extension corpus under `--64` show no "refused".  After: 020.
- [ ] **AS-T-022** If an instruction that takes an operand is written with none, then the assembler shall report an error.
  Trace: AS-X86-040.  Verify: T — `inc`, `dec`, `push`, `pop`, `neg`, `not`, `mul`, `div`, `call`, `jmp` bare: exit 1, a message, nothing from the sanitizer build.
- [ ] **AS-T-023** The lexer shall not read a byte beyond the terminator of the line it is given.
  Trace: AS-FE-020.  Verify: T — a file whose only line is `{vex}`, and the same for `{evex}`, `{disp8}`, `{disp32}`, under the sanitizer build.
- [ ] **AS-T-024** When `.pushsection` is given a number as its second argument, the assembler shall select that subsection and shall leave the section's flags as they were.
  Trace: AS-SEC-001.  Verify: T — `.text` / `nop` / `.pushsection .text, 1` / `ret` / `.popsection` / `nop`: `.text` is `AX` and holds `90 90 c3`.
- [ ] **AS-T-025** When a directive is written with an empty argument, the assembler shall keep the argument's position.
  Trace: AS-FE-002.  Verify: T — `.byte 1,,2` is refused as GNU refuses it; `.p2align 4,,10` reaches the directive with three arguments.
- [ ] **AS-T-026** When an alignment directive names a maximum skip and the padding needed exceeds it, the assembler shall emit no padding.
  Trace: AS-FE-002.  Verify: T — `nop` / `.p2align 4,,10` / `nop` is `90 90`; `.p2align 4,,15` pads; `.balign 8,,3` likewise.  After: 025.
- [ ] **AS-T-027** If a local label reference has no definition in the direction it names, then the assembler shall fail and name the reference and its line.
  Trace: AS-FE-003.  Verify: T — `jmp 1f` with no `1:`; `call 2b` before any `2:`.
- [ ] **AS-T-028** When `*` precedes an operand of `call` or `jmp` that is not a register, the assembler shall encode an indirect branch through that memory operand.
  Trace: AS-FE-005.  Verify: T — `call *foo`, `jmp *foo`, `call *foo(%ebx)`, and under `--64` `call *foo(%rip)`, `jmp *foo`: GNU.
- [ ] **AS-T-029** When `nopw` or `nopl` is written with a memory operand, the assembler shall encode opcode `0F 1F /0`.
  Trace: AS-SEL-009.  Verify: T — the eleven multi-byte NOP forms gcc and GNU `as` pad with, both modes.

### B.2 Prefixes

- [ ] **AS-T-030** When a `lock` prefix is written before an instruction whose destination is memory, the assembler shall emit `F0` before the instruction.
  Trace: AS-SEL-001.  Verify: T — `lock` with `cmpxchg`, `cmpxchg8b`, `cmpxchg16b`, `xadd`, `bts`, `btr`, `btc`, `inc`, `dec`, `add`, `or`, `adc`, `sbb`, `and`, `sub`, `xor`, `neg`, `not`, `xchg`, each width, both modes: GNU.
- [ ] **AS-T-031** When a `rep`, `repe`, `repne` or segment prefix is written before an instruction encoded by the emitter's own tables, the assembler shall emit it.
  Trace: AS-SEL-001.  Verify: T — the callers of the prefixed-`0F` emitter with each prefix: GNU.
- [ ] **AS-T-032** If `lock` is written before an instruction or operand form the processor does not allow it on, then the assembler shall refuse it.
  Trace: AS-X86-026.  Verify: T — `lock addl %eax,%ebx`, `lock movl (%eax),%ebx`, `lock nop`: exit 1.
- [ ] **AS-T-033** When a prefix mnemonic is written as a statement of its own, the assembler shall apply it to the next instruction.
  Trace: AS-FE-040, AS-X86-041.  Verify: T — `lock; cmpxchgl %ebx,(%eax)`, `rep; nop`, `lock` on a line then `incl (%eax)` on the next: GNU.
- [ ] **AS-T-034** When a legacy prefix and a `REX` prefix both apply, the assembler shall emit the legacy prefix first.
  Trace: AS-SEL-006, AS-X86-024, AS-EXT-005.  Verify: T — `paddb %xmm1,%xmm10`, `pxor %xmm8,%xmm8`, the 43 SSE2-integer mnemonics with `%xmm8`–`15` and `%r8`–`15`, `push %r9w`, `movq %rax,%xmm0`, `movq %xmm0,%rax`: GNU.
- [ ] **AS-T-035** When a 16-bit instruction is assembled in 64-bit mode, the assembler shall emit one `66` prefix.
  Trace: AS-X86-011.  Verify: T — every `w`-suffixed line of the 64-bit corpus has exactly one `66`; `movabs %ax,sym` has one.
- [ ] **AS-T-036** When `cbw`, `cbtw`, `cwd`, `cwtd`, `iretw`, `lretw`, `pushaw`, `popaw`, `pushfw`, `popfw`, `jmpw *r16`, `callw *r16`, `cmovccw`, `leaw` or `xaddw` is written, the assembler shall emit the `66` prefix in 32- and 64-bit modes.
  Trace: AS-X86-011, AS-SEL-014.  Verify: T — each: GNU.
- [ ] **AS-T-037** When `jcxz` is assembled in 32-bit mode, or `jecxz` in 64-bit mode, the assembler shall emit the `67` prefix.
  Trace: AS-X86-023.  Verify: T — `jcxz 1f`, `jecxz 1f`, `jrcxz 1f` in each mode: GNU.

### B.3 Wrong instruction

- [ ] **AS-T-038** When `adcb` or `sbbb` is written, the assembler shall encode `adc` or `sbb`.
  Trace: AS-X86-020.  Verify: T — register, memory and immediate forms, with and without `lock`: GNU.
- [ ] **AS-T-039** When `ret`, `retw`, `retl` or `retq` is given an immediate, the assembler shall encode `C2` followed by the 16-bit count.
  Trace: AS-X86-021, AS-OBJ-013.  Verify: T — `ret $4`, `ret $0`, `retw $8`, `--64` `ret $16`, `retq $8`: GNU.
- [ ] **AS-T-040** If an instruction is written with more operands than any of its forms takes, then the assembler shall refuse it.
  Trace: AS-OBJ-013, AS-EXT-010.  Verify: T — `ret $4, $5`, `nop %eax,%ebx,%ecx,%edx`, `lahf -0x100(%rbx),%ebx,%ecx`, `haddps $1,%xmm1,%zmm2,%zmm3`: exit 1.
- [ ] **AS-T-041** When `enter` is written, the assembler shall encode the frame size as the 16-bit immediate and the nesting level as the 8-bit one.
  Trace: AS-X86-023, AS-SEL-014.  Verify: T — `enter $8,$0`, `enter $8,$1`, `enter $0x100,$3`, both modes: GNU.
- [ ] **AS-T-042** When `xchgb` is written, the assembler shall encode the 8-bit exchange.
  Trace: AS-X86-023, AS-SEL-014.  Verify: T — `xchgb %al,%bl`, `xchgb %al,(%eax)`, `xchgl (%ebx),%ebx`: GNU.
- [ ] **AS-T-043** When `movsx` or `movzx` is written with or without size suffixes, the assembler shall take the source width from the suffix or the source register and the destination width from the destination register.
  Trace: AS-X86-023, AS-SEL-009.  Verify: T — `movsxw %ax,%ebx`, `movsx %al,%bx`, `movzx %ax,%eax`, `movsbw`, `movzwl`, `--64` `movsxl %eax,%rbx`, `movslq`: GNU.
- [ ] **AS-T-044** When `ud2b` is written, the assembler shall encode `0F B9`.
  Trace: AS-X86-026.  Verify: T.
- [ ] **AS-T-045** When a rotate or shift is written with `%cl` as its count, the assembler shall encode the by-`CL` form.
  Trace: AS-X86-026.  Verify: T — `rolw %cl,%ax`, `rol %cl,%eax`, `shrb %cl,(%eax)`, each of the eight operations: GNU.
- [ ] **AS-T-046** If both operands of `test` are immediates, then the assembler shall refuse the instruction.
  Trace: AS-X86-026.  Verify: T — `testb $1,$2`: exit 1.
- [ ] **AS-T-047** When `in` or `out` is written with `%al`, `%ax` or `%eax`, the assembler shall encode the form of that width.
  Trace: AS-SEL-014, AS-X86-010.  Verify: T — `in %dx,%al` (`EC`), `out %al,%dx` (`EE`), `in $0x60,%al` (`E4 60`), the `%ax` forms with `66`, both modes: GNU.
- [ ] **AS-T-048** When `nop` is given a memory or register operand, the assembler shall encode the long NOP of that operand.
  Trace: AS-SEL-014.  Verify: T — `nop (%eax)`, `nopw %ax`, `nopl %eax`: GNU.
- [ ] **AS-T-049** When `maskmovq` or `maskmovdqu` is written, the assembler shall place the mask register in ModRM.rm and the source in ModRM.reg.
  Trace: AS-SEL-009.  Verify: T — both, and `vmaskmovdqu`: GNU.
- [ ] **AS-T-050** When `extrq` or `insertq` is written with two immediates, the assembler shall emit them in the order the manual gives.
  Trace: AS-SEL-009.  Verify: T — both: GNU.
- [ ] **AS-T-051** When `aesencwide256kl` is written, the assembler shall encode opcode extension `/2`.
  Trace: AS-SEL-009.  Verify: T — the four wide Key Locker instructions: GNU.
- [ ] **AS-T-052** When `crc32` is written, the assembler shall take the source width from the suffix or the source register.
  Trace: AS-SEL-009, AS-EXT-008.  Verify: T — `crc32b mem,%ebx` (`F0`), `crc32 %al,%ebx`, `crc32 %ax,%ebx` (with `66`), `crc32w (%r12),%ebx`, `crc32 %sil,%ebx` (with `REX`), `crc32q`: GNU.
- [ ] **AS-T-053** When `montmul`, `xstore-rng`, `umonitor %cx`, `pshufd $-1` or `tpause %ecx` is written, the assembler shall encode it as GNU does.
  Trace: AS-SEL-009.  Verify: T — each.
- [ ] **AS-T-054** When a string instruction is written with no operands, the assembler shall encode it with no segment override.
  Trace: AS-SEL-014, AS-X86-041.  Verify: T — bare `movsb`, `movsw`, `movsl`, `movsq`, `stos*`, `lods*`, `scas*`, `cmps*`, `ins*`, `outs*`, with `rep`/`repe`/`repne`, both modes: GNU.
- [ ] **AS-T-055** When a string instruction is written with operands, the assembler shall emit a segment override for the source operand's segment only where it differs from the default, and none for the `%es` destination.
  Trace: AS-X86-025.  Verify: T — `movsl (%esi),%es:(%edi)` (`A5`), `movsw %cs:(%esi),%es:(%edi)` (`2E 66 A5`), `lodsb %fs:(%esi)`: GNU.
- [ ] **AS-T-056** If a string instruction's destination is written with a segment other than `%es`, then the assembler shall refuse it.
  Trace: AS-X86-025.  Verify: T — `stosl %eax,%ds:(%edi)`: exit 1.
- [ ] **AS-T-057** When `xlat`, `aam`, `aad`, `loop`, `loope`, `loopne` or `jecxz` is written in a form GNU accepts, the assembler shall encode it.
  Trace: AS-X86-041, AS-FE-032.  Verify: T — bare `xlat`, `aam`, `aad`, `aam $10`, `loop 1b`, `jecxz 1b`: GNU.

### B.4 Byte registers and immediates

- [ ] **AS-T-058** When `%ah`, `%ch`, `%dh` or `%bh` is the register operand of an instruction whose other operand is an absolute or index-only memory operand, the assembler shall encode that register.
  Trace: AS-X86-022.  Verify: T — `addb %bh,sym`, `movb %ch,(,%eax,4)`, each of the four with each ModRM path: GNU.
- [ ] **AS-T-059** When `%spl`, `%bpl`, `%sil` or `%dil` is an operand, the assembler shall emit a `REX` prefix.
  Trace: AS-X86-022, AS-EXT-008.  Verify: T — `movb %sil,%dil`, `movb %al,%sil`, `cmpb %dil,%sil`, `incb %dil`, `testb`, `setcc %sil`: GNU.
- [ ] **AS-T-060** If `%ah`, `%ch`, `%dh` or `%bh` is used in an instruction that needs a `REX` prefix, then the assembler shall refuse it.
  Trace: AS-X86-022.  Verify: T — `movb %ah,%r8b`, `movb %ah,%sil`, `addb %bh,(%r9)`: exit 1.
- [ ] **AS-T-061** When an instruction has an 8-bit operand and an immediate, the assembler shall emit an 8-bit immediate.
  Trace: AS-X86-012.  Verify: T — `--64` `movb $1,%al` is two bytes; `movb $1,(%rax)`; the ALU group with `b`: GNU.
- [ ] **AS-T-062** When an instruction has a 16-bit operand and an immediate, the assembler shall emit a 16-bit immediate, or the sign-extended 8-bit form.
  Trace: AS-X86-012.  Verify: T — `addw $0x1000,%bx`, `cmpw $0x1234,(%rax)`, `andw`, `pushw $0x1234`, `movw $1,%ax`, both modes: GNU.
- [ ] **AS-T-063** If an immediate does not fit the field its instruction gives it, then the assembler shall refuse it.
  Trace: AS-X86-013.  Verify: T — `addq $0xffffffff,%rax`, `addq $0x80000000,%rbx`, `pushq $0x80000000`, `movq $0x123456789,(%rax)`, `int $256`, `enter $0x10000,$0`, `shl $-1,%eax`, `movb $256,%al`: exit 1, as GNU.
- [ ] **AS-T-064** When a value is written to `.byte`, `.word` or `.long` that does not fit, the assembler shall warn and emit the truncated value.
  Trace: AS-DAT-003.  Verify: T — `.byte 256`, `.word 65536`, `.long 0x100000000`: a warning on standard error, exit 0, GNU's bytes.

### B.5 x86-64 addressing

- [ ] **AS-T-065** When `%r12` is written as an index register, the assembler shall encode it.
  Trace: AS-X86-024.  Verify: T — `movl %eax,(%rax,%r12)`, `(%rbx,%r12,8)`, `(,%r12,4)`; `%rsp` as index still refused.
- [ ] **AS-T-066** When `cvtsi2sd`, `cvtsi2ss` or their VEX forms are written with a 32-bit source, the assembler shall not set `REX.W` or `VEX.W`.
  Trace: AS-SEL-006, AS-X86-024, AS-EXT-008.  Verify: T — `cvtsi2sd %eax,%xmm0`, `cvtsi2sdl (%rax),%xmm0`, `cvtsi2sdq %rax,%xmm0`, `vcvtsi2sdq %rax,%xmm1,%xmm2`, `vcvtusi2sdl`: GNU.

### B.6 Directives that do nothing

- [ ] **AS-T-067** When `.int`, `.value`, `.2byte`, `.4byte` or `.8byte` is written, the assembler shall emit its arguments at 4, 2, 2, 4 and 8 bytes.
  Trace: AS-OBJ-008.  Verify: T — `.byte 0xEE` / `.int 5` / `.byte 0xFF` is `ee 05 00 00 00 ff`; each of the five.
- [ ] **AS-T-068** When `.dc.b`, `.dc.w`, `.dc.l`, `.dc.q`, `.dc.a`, `.ds.*` or `.dcb.*` is written, the assembler shall emit what GNU `as` emits.
  Trace: AS-OBJ-008.  Verify: T — each.
- [ ] **AS-T-069** When `.octa` is written, the assembler shall emit 16 bytes for each argument.
  Trace: AS-OBJ-008.  Verify: T — a value below and above 2^64.
- [ ] **AS-T-070** When `.uleb128` or `.sleb128` is written, the assembler shall emit the LEB128 encoding of each argument.
  Trace: AS-OBJ-008.  Verify: T — 0, 127, 128, -1, -129, 2^63, and a difference of two labels in one section.
- [ ] **AS-T-071** When `.single`, `.tfloat` or `.string16` is written, the assembler shall emit what GNU `as` emits.
  Trace: AS-OBJ-008.  Verify: T — each.
- [ ] **AS-T-072** When `.balignw`, `.balignl`, `.p2alignw` or `.p2alignl` is written, the assembler shall pad with the 2- or 4-byte fill pattern.
  Trace: AS-OBJ-008, AS-SEC-006.  Verify: T — each, with and without a fill and a maximum.
- [ ] **AS-T-073** When `.struct` is written, the assembler shall define the labels that follow as absolute offsets.
  Trace: AS-OBJ-008.  Verify: T — the offsets idiom (`.struct 0` / `a: .struct a+4` / `b:`).
- [ ] **AS-T-074** If a directive is not one the assembler implements, then the assembler shall fail and name the directive and its line.
  Trace: AS-OBJ-008, AS-FE-010.  Verify: T — `.foobar 1,2`: exit 1; no directive name returns success from a default branch (I — the two default returns of AS-OBJ-008).  After: 067–073.
- [ ] **AS-T-075** When `.error` or `.err` is assembled, the assembler shall print the message and fail.
  Trace: AS-FE-010.  Verify: T — `.error "boom"`: "boom" on standard error, exit 1; inside a false conditional, nothing.
- [ ] **AS-T-076** When `.abort` is assembled, the assembler shall stop and fail.
  Trace: AS-FE-010.  Verify: T.
- [ ] **AS-T-077** When `.warning` is assembled, the assembler shall print the message and continue.
  Trace: AS-FE-010.  Verify: T.

### B.7 Code that vanishes

- [ ] **AS-T-078** When an instruction is written in a section, the assembler shall assemble it into that section whatever the section's flags.
  Trace: AS-OBJ-009.  Verify: T — `call f` / `ret` in `.init`, `.fini`, `.section .foo,"a"`, `.data`, `.gnu.linkonce.t.x`: the bytes and the relocation are there.
- [ ] **AS-T-079** When a section of a well-known name is opened without flags, the assembler shall give it the flags and type GNU `as` gives it.
  Trace: AS-OBJ-009.  Verify: T — `.init`, `.fini` (`AX`); `.tdata` (`WAT`), `.tbss` (`WAT`, `NOBITS`); `.init_array`, `.fini_array`, `.preinit_array` (their types, `WA`); `.debug_*`, `.comment`, `.note.GNU-stack` (not allocated); `.eh_frame` (`A`); `.rodata*`, `.data.rel.ro*`, `.bss.*`, `.text.*`, `.gnu.linkonce.*` by prefix.
- [ ] **AS-T-080** When a section's flags string holds `e`, `R`, `o`, `T`, `?` or a numeric value, the assembler shall set the flag each names.
  Trace: AS-OBJ-009, AS-SEC-005.  Verify: T — each against `readelf -S` of GNU's object.
- [ ] **AS-T-081** If data other than zero is written into a `NOBITS` section, then the assembler shall fail.
  Trace: AS-OBJ-016, AS-SEC-005.  Verify: T — `.bss` / `.long 5`: exit 1; `.bss` / `.zero 8` and `.skip 8`: size 8.

### B.8 The other targets' names

- [ ] **AS-T-082** While assembling for x86, the assembler shall treat `sp`, `lr`, `pc`, `cpsr`, `spsr`, `xzr`, `wzr` and names of the form `r`/`w`/`x`/`q`/`d`/`s`/`v` followed by digits as symbols.
  Trace: AS-ARM-003.  Verify: T — `call sp` (a relocation against `sp`), `movl sp,%eax`, `jmp x1`, `movl s1,%eax`, `call r12` under `--64`: GNU; `int s1; int x1(void){return s1;}` from `gcc -m32 -fno-pie -S` assembles.
- [ ] **AS-T-083** The build shall not install the assembler under the names `arm-as` and `aarch64-as` while it cannot assemble for those targets.
  Trace: AS-ARM-001.  Verify: I — `Makefile` and `build-rootfs.sh` install neither; the images hold neither.  (Undone by section L if that is taken up.)
- [ ] **AS-T-084** If the assembler is asked for a target it cannot assemble for, by name or by option, then it shall fail and write no object.
  Trace: AS-ARM-002.  Verify: T — `--target=aarch64`, `-marm`, `-mthumb`, `-mcpu=cortex-a53`, and `argv[0]` of `arm-as`: exit 1.

## C. Read the source once (front-end structure)

The three textual passes through temporary files go; the source becomes
statements that know their file and line, and conditionals are decided
with a symbol table.

- [ ] **AS-T-090** The assembler shall read each input into statements that each carry the file name and line number the user would recognise.
  Trace: AS-FE-050, AS-FE-030.  Verify: I — no statement is constructed from a temporary file's coordinates; T — every message in the suite names the input file.
- [ ] **AS-T-091** The assembler shall expand macros, repeats and conditionals without writing intermediate files.
  Trace: AS-FE-050, AS-FE-024.  Verify: D — `strace -e trace=openat` over the suite opens no file the user did not name, but for the output's temporary.  After: 090.
- [ ] **AS-T-092** The assembler shall have one scanner of quoted strings and comments, which honours backslash escapes.
  Trace: AS-FE-050.  Verify: I — the six scanners of AS-FE-050 are one function.
- [ ] **AS-T-093** When `;` occurs inside a comment or a quoted string, the assembler shall not begin a statement there.
  Trace: AS-FE-009.  Verify: T — `nop # c; hlt` is `90`; `.ascii "a\";b"` is `61 22 3b 62`; a C string holding `\";` compiled by gcc assembles.  After: 092.
- [ ] **AS-T-094** The lexer shall produce a token for each operator, punctuation mark, register, number, name and string of a statement.
  Trace: AS-FE-051.  Verify: T — a lexer unit test: `a+b`, `%fs:0`, `x=5`, `*%eax`, `$sym`, `%gs : 8` give the tokens listed in the test.
- [ ] **AS-T-095** The parser shall build operands and expressions from tokens and shall not scan text.
  Trace: AS-FE-051.  Verify: I — `join_tokens` and the `strchr` scans for `:`, `=`, `*` are gone from `as_parser.c`; `as_expr.c` takes tokens.  After: 094.
- [ ] **AS-T-096** The assembler shall hold the x86 register names in one table that gives each its class, width and number.
  Trace: AS-FE-051, AS-DES-004.  Verify: I — the tables of `as_lexer.c`, `as_parser.c` and the encoders are one.
- [ ] **AS-T-097** When a `# N "file"` line of the C preprocessor is read, the assembler shall take N and the file as the coordinates of the lines that follow.
  Trace: AS-FE-030.  Verify: T — an error in a `.S` file with an `#include` above it names the `.S` file and the right line.  After: 090.
- [ ] **AS-T-098** When a line ends in a backslash, the assembler shall join the next line to it.
  Trace: AS-FE-040.  Verify: T.
- [ ] **AS-T-099** When two or more labels are written on one line, the assembler shall define each.
  Trace: AS-FE-013.  Verify: T — `q1:q2: .long 4`; `a: b: c: nop`.  After: 094.
- [ ] **AS-T-100** When a symbol name is written in double quotes, the assembler shall take the quoted text as the name.
  Trace: AS-FE-040.  Verify: T — `"a b": .long "a b"`.
- [ ] **AS-T-101** When a character constant is written after `$` or within an expression, with or without its closing quote, the assembler shall take its value.
  Trace: AS-FE-040.  Verify: T — `movb $'a',%al`, `movb $'a,%al`, `.byte 'a, 'a+1`, `'\n`.
- [ ] **AS-T-102** When a line holds a NUL byte, the assembler shall fail and name the line.
  Trace: AS-FE-025.  Verify: T.

### C.1 Conditionals and symbols

- [ ] **AS-T-103** When `.ifdef` or `.ifndef` names a symbol, the assembler shall decide by whether the symbol is defined at that point.
  Trace: AS-FE-007.  Verify: T — after `.set X,1`; after a label; for an undefined name; for a `--defsym`.  After: 090.
- [ ] **AS-T-104** When the expression of `.if`, `.ifeq`, `.ifne`, `.ifgt`, `.ifge`, `.iflt`, `.ifle` or `.elseif` names symbols with absolute values, the assembler shall decide by the expression's value.
  Trace: AS-FE-007.  Verify: T — `.set X,0` / `.if X` false; `.if X==0` true; `.if K*2 > 3`.  After: 090.
- [ ] **AS-T-105** If the expression of a conditional has no absolute value, then the assembler shall fail and say so.
  Trace: AS-FE-007.  Verify: T — `.if undefined_symbol`, `.if label_in_text`.  After: 104.
- [ ] **AS-T-106** When `.ifc`, `.ifnc`, `.ifb`, `.ifnb`, `.ifeqs` or `.ifnes` is written, the assembler shall compare its operands as text.
  Trace: AS-FE-007.  Verify: T — each, in and out of a macro; the textual fallback of `.if` (`as.c`, `eval_gas_cond_expr`) is removed with it (I).  After: 104.
- [ ] **AS-T-107** While a conditional is false, the assembler shall not define the macros, symbols or sections written inside it.
  Trace: AS-FE-008.  Verify: T — a `.macro` inside `.if 0` is not defined; a label inside is not defined.  After: 090.
- [ ] **AS-T-108** If a conditional is unmatched or unclosed, then the assembler shall fail and name the directive and the line where it began.
  Trace: AS-FE-031.  Verify: T — `.endif` alone; `.if 1` to end of file; `.else` twice.

### C.2 Macros and repeats

- [ ] **AS-T-109** If a macro is defined twice, then the assembler shall fail and name both definitions.
  Trace: AS-FE-008.  Verify: T — as GNU; `.purgem` then a new definition is accepted.
- [ ] **AS-T-110** When `.purgem` is assembled, the assembler shall remove the macro.
  Trace: AS-FE-010.  Verify: T.
- [ ] **AS-T-111** When `.exitm` is assembled, the assembler shall end the innermost macro expansion.
  Trace: AS-FE-010.  Verify: T — a recursive macro that ends itself with `.if` / `.exitm` terminates.
- [ ] **AS-T-112** When a macro parameter is declared `:req`, and the macro is called without it, the assembler shall fail.
  Trace: AS-FE-012.  Verify: T.
- [ ] **AS-T-113** When a macro's last parameter is declared `:vararg`, the assembler shall give it all remaining arguments with their commas.
  Trace: AS-FE-012.  Verify: T.
- [ ] **AS-T-114** When a macro argument is quoted, the assembler shall pass it as GNU `as` passes it.
  Trace: AS-FE-012.  Verify: T — `m "a b"` used as `.ascii \x` and as an operand.
- [ ] **AS-T-115** When `\@` is expanded, the assembler shall give the count of macro expansions so far, starting at 0.
  Trace: AS-FE-012.  Verify: T.
- [ ] **AS-T-116** When a `.macro` is written inside a macro body, the assembler shall match each `.endm` to its own `.macro`.
  Trace: AS-FE-012.  Verify: T — a macro that defines a macro.
- [ ] **AS-T-117** If the input ends inside `.macro`, `.rept`, `.irp` or `.irpc`, then the assembler shall fail and name the line where it began.
  Trace: AS-FE-012.  Verify: T — each.
- [ ] **AS-T-118** When `.altmacro` is written, the assembler shall implement it or fail.
  Trace: AS-FE-010.  Verify: T — exit 1 with "not supported" is acceptable.
- [ ] **AS-T-119** When `.string`, `.ascii` or `.asciz` is given adjacent strings, the assembler shall concatenate them with nothing between.
  Trace: AS-FE-012, AS-OBJ-016.  Verify: T — `.ascii "a" "b"` is `61 62`.
- [ ] **AS-T-120** If expansion would exceed `--max-expansion-bytes` (a new limit, default 256 MiB of statements), then the assembler shall fail.
  Trace: AS-FE-022.  Verify: T — `.rept 0x7fffffff` / `.endr` and two nested `.rept 200000` fail within a second; `as(1)` documents the option.  After: 091.
- [ ] **AS-T-121** If macro expansion nests deeper than `--max-macro-depth`, then the assembler shall fail and name the macro.
  Trace: AS-FE-023.  Verify: T — the option, not a constant 64, is the limit.
- [ ] **AS-T-122** The limits `--max-line-bytes`, `--max-token-length` and `--max-include-depth` shall each mean what `as(1)` says, and shall refuse a value that is not a positive number.
  Trace: AS-FE-023.  Verify: T — a line of exactly the limit passes and one more fails; `--max-line-bytes=-1` is refused; the man page states each definition.

### C.3 Includes

- [ ] **AS-T-123** When `.include "f"` is assembled, the assembler shall assemble the contents of f in its place.
  Trace: AS-FE-001.  Verify: T — `.include "inc2.s"` then `cli`, with `nop; hlt` in the file: `90 f4 fa`.  After: 090.
- [ ] **AS-T-124** If the file of an `.include` cannot be opened, then the assembler shall fail and name the file and the including line.
  Trace: AS-FE-001.  Verify: T.  After: 123.
- [ ] **AS-T-125** When `.include` names a relative path, the assembler shall look in the including file's directory and then in each `-I` directory in order.
  Trace: AS-FE-001.  Verify: T — a file found each way; the temporary directory is never searched.  After: 123.
- [ ] **AS-T-126** If inclusion nests deeper than `--max-include-depth`, then the assembler shall fail and name the chain.
  Trace: AS-FE-001.  Verify: T — a file that includes itself.  After: 123.
- [ ] **AS-T-127** The assembler shall expand macros, repeats and conditionals in included text as in the including file, with macros defined in either visible in both.
  Trace: AS-FE-001.  Verify: T.  After: 123.
- [ ] **AS-T-128** A local label reference shall resolve to the nearest definition in assembly order, across the boundary of an included file.
  Trace: AS-FE-001.  Verify: T — `1:` in the including file, `jmp 1b` in the included.  After: 123.
- [ ] **AS-T-129** When `.incbin "f", skip, count` is written, the assembler shall emit count bytes of f from skip, and shall fail if the file is shorter than skip.
  Trace: AS-DAT-003.  Verify: T — the three argument forms; a skip past the end.

### C.4 Operands as written

- [ ] **AS-T-130** If the base or index of a memory operand is not a register, then the assembler shall refuse the operand.
  Trace: AS-FE-011.  Verify: T — `movl (bad),%eax`: exit 1.
- [ ] **AS-T-131** If text follows the closing parenthesis of a memory operand, or the operand has more than three components, then the assembler shall refuse it.
  Trace: AS-FE-011.  Verify: T — `movl (%eax)junk,%ebx`, `movl (%eax,%ebx,2,junk),%ecx`.
- [ ] **AS-T-132** If an instruction has an empty operand, then the assembler shall refuse it.
  Trace: AS-FE-011.  Verify: T — `movl $1,,%eax`, `movl $1,%eax,`.
- [ ] **AS-T-133** If a scale factor is not 1, 2, 4 or 8, then the assembler shall refuse the operand.
  Trace: AS-SEL-010.  Verify: T — `(%eax,%ebx,3)` in every encoder path.
- [ ] **AS-T-134** When a local label is used as an immediate, an absolute address or a displacement, the assembler shall give the label's address, with a relocation where one is needed.
  Trace: AS-FE-004, AS-LAY-003.  Verify: T — `pushl $1f`, `movl $1f,%eax`, `leal 1f,%eax`, `movl 1f,%eax`, `movl 1f(%ebx),%eax`, `movq $1f,%rax`: GNU.
- [ ] **AS-T-135** When a local label is an argument of a data directive, the assembler shall emit a relocation against the label's section with the label's offset.
  Trace: AS-FE-004, AS-LAY-003.  Verify: T — `.long 1b, 2f`, `.quad 1b`: no undefined symbol named `1b`.
- [ ] **AS-T-136** When a local label is defined in a section other than the one that refers to it, the assembler shall resolve the reference with a relocation.
  Trace: AS-LAY-003.  Verify: T — `.data` / `1: .long 0` / `.text` / `jmp 1b` / `call 1b` / `movl $1b,%eax`: GNU.
- [ ] **AS-T-137** When a branch names an absolute address, the assembler shall emit a PC-relative relocation against the absolute section.
  Trace: AS-LAY-003, AS-X86-026.  Verify: T — `jmp 0x1234`, `call 0x1000`, `je 0x1000`: GNU.

### C.5 Command line

- [ ] **AS-T-138** If an option is not one the assembler implements, then the assembler shall fail and name it.
  Trace: AS-FE-014.  Verify: T — `--bogus-option`: exit 1.
- [ ] **AS-T-139** When `--defsym NAME=VALUE` is given, the assembler shall define NAME absolute with that value before assembly.
  Trace: AS-FE-014.  Verify: T — `.long X`, `.if X`, `.ifdef X`.
- [ ] **AS-T-140** When the input file is `-` or none is given, the assembler shall read standard input.
  Trace: AS-FE-014, AS-FE-040.  Verify: T — `gcc -pipe -c` with this assembler as `as`.
- [ ] **AS-T-141** When more than one input file is given, the assembler shall assemble them as one, in order.
  Trace: AS-FE-040.  Verify: T.
- [ ] **AS-T-142** When `--fatal-warnings` is given and a warning is issued, the assembler shall fail.
  Trace: AS-FE-014.  Verify: T — with the warning of 064.
- [ ] **AS-T-143** When `-march=` names an x86 processor GNU `as` knows, the assembler shall accept it and enable that processor's instruction sets.
  Trace: AS-FE-014.  Verify: T — `-march=haswell`, `i686`, `x86-64-v2`, `znver3`; an unknown name is refused.
- [ ] **AS-T-144** When `--version` is given, the assembler shall name itself as the Substrate assembler and shall not claim to be GNU Binutils.
  Trace: AS-FE-014.  Verify: T; D — a GCC `configure` run with it as `as` detects only the features it has.
- [ ] **AS-T-145** Each of `-g`, `--gdwarf-N`, `-D`, `--warn`, `--no-warn`, `-W`, `--from-cc` shall take the effect `as(1)` gives it, or be removed from the options accepted.
  Trace: AS-FE-014.  Verify: I — the man page and the option table agree; T — one case each.
- [ ] **AS-T-146** When no `-o` is given, the assembler shall write `a.out`.
  Trace: AS-FE-032.  Verify: T.

### C.6 Messages and temporary files

- [ ] **AS-T-147** Every error message shall have the form `file:line: Error: text`, with text that is not empty.
  Trace: AS-FE-030, AS-FE-032.  Verify: T — a test that greps every failing case of the suite for the form; `loop 1b` to an undefined label has text.
- [ ] **AS-T-148** When a failure is found while expanding a macro or repeat, the message shall name the user's line and the macro being expanded.
  Trace: AS-FE-031.  Verify: T — a bad `.macro` header; a `.rept` whose count is a symbol; an error inside an expansion.  After: 090.
- [ ] **AS-T-149** When the assembler fails, it shall remove the output file it was asked to write.
  Trace: AS-FE-032.  Verify: T — an old `out.o` is gone after a failed run, as with GNU.
- [ ] **AS-T-150** When the assembler ends by any path, including a signal it can catch, it shall leave no file it created but the output.
  Trace: AS-FE-024.  Verify: T — `SIGINT`, `SIGTERM` and an error exit, with `TMPDIR` pointing at an empty directory that is empty afterwards.
- [ ] **AS-T-151** Where the assembler needs a temporary file, it shall create it in `$TMPDIR`, or in `/tmp` when that is unset.
  Trace: AS-FE-024.  Verify: T.
- [ ] **AS-T-152** Every error path of the parser shall free what it allocated.
  Trace: AS-FE-025.  Verify: T — the failing cases of the suite under LeakSanitizer report nothing.
- [ ] **AS-T-153** `main` shall release its resources in one place.
  Trace: AS-FE-052.  Verify: I — the clean-up repeated some forty-five times is one block.
- [ ] **AS-T-154** The front end shall hold no code that cannot run.
  Trace: AS-FE-052.  Verify: I — the list in AS-FE-052 is gone or reachable; A — a coverage run of the suite shows no function of `as.c`, `as_lexer.c`, `as_parser.c` with zero calls.  After: 123.

## D. An operand model, and one x86 encoder

A register has a class and a width; an instruction is found by its name in
one table of templates; there is one ModRM/SIB/prefix emitter.

- [ ] **AS-T-160** The assembler shall represent each x86 operand with its kind and, for a register, its class (general, segment, control, debug, x87, MMX, XMM, YMM, ZMM, opmask, bound) and width.
  Trace: AS-X86-010, AS-DES-004.  Verify: I; T — a unit test of the operand converter over every register name.  After: 096.
- [ ] **AS-T-161** The assembler shall find an x86 instruction by one lookup of its mnemonic in a table of templates, each giving operand classes, widths, opcode, prefixes and the modes it is valid in.
  Trace: AS-DES-006, AS-DES-004, AS-PERF-002.  Verify: I — no `strcmp` ladder over mnemonics remains; A — callgrind shows one hash lookup per statement.  After: 160.
- [ ] **AS-T-162** The assembler shall have one emitter of prefixes, `REX`/`VEX`/`EVEX`, ModRM, SIB, displacement and immediate, used by every x86 instruction.
  Trace: AS-DES-004, AS-DES-006, AS-EXT-004.  Verify: I — the copies of `modrm_sib_disp64` in nine files, the three in `as_elf_emit.c` and the one in `as_x86_encode.c` are one.  After: 160.
- [ ] **AS-T-163** One mnemonic shall be encoded in one place.
  Trace: AS-X86-003, AS-DES-004.  Verify: I — each duplicate named in AS-X86-003 is gone; the branches made unreachable by suffix stripping are gone.  After: 161.
- [ ] **AS-T-164** `as_elf_emit.c` shall hold no instruction selection or encoding.
  Trace: AS-DES-004, AS-DES-003.  Verify: I — lines 1–7505 of the audited file are in encoder files or gone.  After: 161, 162.
- [ ] **AS-T-165** The assembler shall encode each statement once.
  Trace: AS-PERF-002.  Verify: A — callgrind on 3,000 instructions: under 5,000 machine instructions per source instruction; D — 200,000 `addl` lines in under 1 second on the reference host.  After: 161, 190.
- [ ] **AS-T-166** When an encoder refuses a statement, the message the user sees shall be that encoder's.
  Trace: AS-DES-006, AS-ARM-008.  Verify: T — `rorx` with bad operands has a message; no message is overwritten by a later attempt.  After: 161.
- [ ] **AS-T-167** `x86_stmt_requires_v3`/`_v4` shall not encode a statement to learn what it needs.
  Trace: AS-DES-006.  Verify: I — the requirement comes from the matched template.  After: 161.

### D.1 Operand size

- [ ] **AS-T-168** When an instruction is written without a size suffix and has a register operand, the assembler shall take the operand size from the register.
  Trace: AS-X86-010.  Verify: T — `mov %al,%cl`, `mov %ax,%bx`, `add %cl,(%eax)`, `inc %al`, `shl $1,%cl`, and `neg`, `not`, `mul`, `div`, `push`, `pop`, `xchg`, `bt`, `bsf`, `cmov`, `imul` with 8- and 16-bit registers, both modes: GNU.  After: 160.
- [ ] **AS-T-169** If an instruction has no suffix, no register operand and more than one possible size, then the assembler shall refuse it as ambiguous, or take GNU's default where GNU has one.
  Trace: AS-X86-010, AS-SEL-007.  Verify: T — `mov $1,(%eax)` refused; `push $1`, `fisttp (%eax)` (16-bit, AS-SEL-007) take GNU's default.  After: 168.
- [ ] **AS-T-170** If the suffix of an instruction disagrees with the width of a register operand, then the assembler shall refuse it.
  Trace: AS-X86-010.  Verify: T — `movl %ax,%ebx`, `addw %al,%bx`, `movsbl %ax,%ebx`.  After: 160.
- [ ] **AS-T-171** If a register operand is not of a class and width the instruction takes in that position, then the assembler shall refuse the instruction.
  Trace: AS-X86-010, AS-SEL-011, AS-EXT-010.  Verify: T — the 1,752 acceptances of the selection sweep and the 427 + 943 "GNU refuses, ours accepts" lines of the core corpus are refused; named cases: `mov %al,%ebx`, `seta %ax`, `lea (%eax),%al`, `bswap %ax`, `jmp *%al`, `pushb %al`, `lgdt %eax`, `hlt %eax`, `nop $1`, `mov $1,%es`, `bsf %xmm1,%xmm2`, `movd %xmm1,%xmm2`, `sgdt %eax`, `movsd %xmm1,%mm2`.  After: 161.
- [ ] **AS-T-172** When a mnemonic is written with a size suffix its instruction takes, the assembler shall accept it.
  Trace: AS-SEL-012, AS-X86-041.  Verify: T — `sall`, `salw`, `salb`, `salq`, `shldl`, `shrdw`, `larl`, `lsll`, `movntil`, `boundl`, `lssl`, `lfsl`, `lgsl`, `iretl`, `lretl`, `movbew`, `movbel`, `adcxl`, `adoxq`, `andnl`, `bzhiq`, `tzcntw`: GNU.  After: 161.
- [ ] **AS-T-173** When `shld` or `shrd` is written with two operands, the assembler shall encode the by-`CL` form.
  Trace: AS-SEL-012.  Verify: T.
- [ ] **AS-T-174** When a 16-bit operand is used with an instruction encoded from the SIMD-era tables, the assembler shall emit the `66` prefix.
  Trace: AS-SEL-005, AS-EXT-008.  Verify: T — `bsfw`, `bsrw`, `popcnt %ax,%bx`, `lzcnt`, `tzcnt`, `btw`, `cmpxchg %bx,(%eax)`, `shld $4,%ax,%bx`, `movbe (%eax),%bx`, `sldtw`, `smsw %ax`, `rdrand %ax`, `pushw %fs`, `movw %cs,%ax`: GNU.  After: 168.
- [ ] **AS-T-175** When `cmpxchg` is written with `%ah`, `%bh`, `%ch` or `%dh`, the assembler shall encode the 8-bit form.
  Trace: AS-SEL-005.  Verify: T.  After: 168.

### D.2 Address size

- [ ] **AS-T-176** When the base or index register of a memory operand is of a width other than the mode's address size, the assembler shall emit the `67` prefix, or refuse where no encoding exists.
  Trace: AS-SEL-010.  Verify: T — `movaps (%bx),%xmm0` in 32-bit mode (16-bit ModRM with `67`), `fldl (%eax)` in 64-bit mode (`67 dd 00`), `movl (%rax),%ebx` in 32-bit mode refused.  After: 162.
- [ ] **AS-T-177** If the base and index of a memory operand are of different widths, then the assembler shall refuse the operand, but for a vector index.
  Trace: AS-SEL-010, AS-EXT-009.  Verify: T — `(%eax,%rbx)` refused; `(%rax,%xmm2,4)` accepted for a gather.  After: 162.

### D.3 Forms that are missing

- [ ] **AS-T-178** When an x87 arithmetic-and-pop instruction is written with the operand orders GNU `as` accepts, the assembler shall encode it, and shall refuse the orders GNU refuses.
  Trace: AS-SEL-007.  Verify: T — `faddp %st,%st(1)`, `faddp`, `fsubp %st,%st(2)`, `fsubrp`, `fmulp`, `fdivp`, `fdivrp`; `fsubp %st(2),%st` refused.  After: 161.
- [ ] **AS-T-179** When an x87 instruction of the following is written, the assembler shall encode it in 32- and 64-bit modes: `fabs`, `fchs`, `fld1`, `fldz`, `fldpi`, `fldl2e`, `fldl2t`, `fldlg2`, `fldln2`, `fsqrt`, `fsin`, `fcos`, `fsincos`, `fptan`, `fpatan`, `frndint`, `fscale`, `fprem`, `fprem1`, `fxtract`, `f2xm1`, `fyl2x`, `fyl2xp1`, `ftst`, `fxam`, `fcompp`, `fucompp`, `fninit`, `finit`, `fnclex`, `fclex`, `fstsw`, `fnstsw`, `fstcw`, `fnstcw`, `fldcw`, `fsave`, `fnsave`, `frstor`, `fstenv`, `fnstenv`, `fldenv`, `fildq`, `fistpq`, `fisttpq`, `fxch`, `fcom`, `fcomp`, `fucom`, `fucomp`, `fcomi`, `fcomip`, `fucomi`, `fucomip`, `fcmovcc`, `ffree`, `fwait`, `fnop`, `fdecstp`, `fincstp`, and `fld`, `fst`, `fstp`, `fild`, `fist`, `fistp`, `fadd`, `fsub`, `fsubr`, `fmul`, `fdiv`, `fdivr`, `fiadd` and their fellows with `s`, `l`, `t` and register operands.
  Trace: AS-SEL-007, AS-X86-001.  Verify: T — a corpus of every listed mnemonic with each operand form GNU accepts.  After: 161.
- [ ] **AS-T-180** When `movq` is written with an MMX or XMM register and another MMX or XMM register, a 64-bit general register or memory, the assembler shall encode the SIMD move.
  Trace: AS-SEL-003, AS-OBJ-013, AS-X86-024, AS-EXT-008.  Verify: T — the twelve forms in 32- and 64-bit modes: GNU (`movq %xmm1,%xmm2` is `f3 0f 7e d1`; `movq %xmm0,(%eax)` is `66 0f d6 00`).  After: 160.
- [ ] **AS-T-181** When an MMX or SSE shift is written with an immediate count, the assembler shall encode the `0F 71`/`72`/`73` form.
  Trace: AS-SEL-004.  Verify: T — `psllw`, `pslld`, `psllq`, `psrlw`, `psrld`, `psrlq`, `psraw`, `psrad`, `pslldq`, `psrldq` with `$n` on MMX and XMM registers, and their VEX forms: GNU.
- [ ] **AS-T-182** If an immediate is written where an instruction takes a register or memory operand, then the assembler shall refuse the instruction.
  Trace: AS-SEL-004.  Verify: T — `flds $3`, `clflush $3`, `fxsave $3`, `prefetcht0 $3`, `minpd $0xab,%xmm1,%xmm2`: exit 1.  After: 171.
- [ ] **AS-T-183** When `cmpsd` is written with XMM operands and an immediate, the assembler shall encode the SSE2 compare.
  Trace: AS-SEL-011.  Verify: T — `cmpsd $1,%xmm1,%xmm2`, and the `cmpeqsd`-style aliases.
- [ ] **AS-T-184** When an instruction of the following is written in a form GNU accepts, the assembler shall encode it: `testl (%ebx),%eax`, `imul $imm,%reg`, `iretl`, moves to and from `%cr8`, `notrack`, `endbr32`, `endbr64`, `xacquire`, `xrelease`, `vmcall`, `xsetbv`, `repz ret`, `mov sym,%es` in 64-bit mode.
  Trace: AS-X86-041.  Verify: T — each.  After: 161.
- [ ] **AS-T-185** When the opmask instructions are written, the assembler shall encode them as the manual gives them, and shall refuse names that are not instructions.
  Trace: AS-SEL-008, AS-EXT-008, AS-EXT-009.  Verify: T — `kmov{b,w,d,q}` with `k`, general-register and memory operands in each direction; `kunpck{bw,wd,dq}`; `knot*`, `kortest*`, `ktest*`, `kshiftl*`, `kshiftr*`; `kand`, `kandfoo`, `kaddz` refused.  After: 161.
- [ ] **AS-T-186** Where an instruction has more than one encoding of the same meaning, the assembler shall choose the shortest.
  Trace: AS-X86-042, AS-EXT-008 (note 19).  Verify: T — sign-extended `imm8` in the ALU group, `6A` push, accumulator short forms, `disp8` for `(%ebp)`, `(%rbp)`, `(%r13)`, `D1` shifts by one, `pushq %rbp` as `55`, no SIB for a bare 32-bit address; the "ours longer" class of both corpora is empty.  After: 162.
- [ ] **AS-T-187** When a branch names a label in its own section, the assembler shall resolve it with no relocation and choose the short form where it reaches.
  Trace: AS-LAY-003.  Verify: T — `jmp l` / `l: ret` is `eb 00 c3` with no relocation.  After: 190.

## E. Fixups: the encoder says where its relocations are

- [ ] **AS-T-190** The encoder shall return, with the bytes of an instruction, a record for each field that holds an expression: its offset, width, whether it is PC-relative, the expression, and the number of bytes of the instruction after it.
  Trace: AS-DES-001.  Verify: I; T — a unit test over forms with a displacement, an immediate, and both.  After: 162.
- [ ] **AS-T-191** The assembler shall make each instruction relocation from a fixup record and from nothing else.
  Trace: AS-DES-001.  Verify: I — the code that derives a relocation's place from the instruction's length and mnemonic is gone.  After: 190.
- [ ] **AS-T-192** When an instruction has a symbolic displacement followed by an immediate, the assembler shall place the relocation on the displacement and keep the immediate.
  Trace: AS-OBJ-001, AS-EXT-004 (note 8).  Verify: T — `movl $5,sym`, `cmpl $0,sym`, `movb $1,sym`, `shll $2,sym`, `testl $1,sym+4`, `pinsrb $3,foo(%rip),%xmm2`: GNU.  After: 191.
- [ ] **AS-T-193** When a PC-relative field is followed by n more bytes of its instruction, the assembler shall give the relocation an addend of −(field width + n).
  Trace: AS-OBJ-001.  Verify: T — `movl $5,sym(%rip)` (−8), `cmpb $1,sym(%rip)` (−5), `cmpw $0x1234,sym(%rip)` (−6): GNU.  After: 191.
- [ ] **AS-T-194** When a memory operand has a symbolic displacement, the assembler shall encode a 32-bit displacement field (16-bit in 16-bit addressing).
  Trace: AS-SEL-002, AS-OBJ-002, AS-EXT-004 (note 6).  Verify: T — `movaps sym(%ebx),%xmm0`, `movdqa sym(%ebp),%xmm1`, `flds sym(%ebx)`, `fnstcw sym(%ebx)`, `haddps foo(%rdx),%xmm2`, `vmulsd foo+4(%rdx),%xmm2,%xmm3`, `andn foo+4(%rdx),%ebx,%ecx`: GNU.  After: 190.
- [ ] **AS-T-195** When an immediate is symbolic, the assembler shall encode an immediate field of the operand's full width.
  Trace: AS-OBJ-002.  Verify: T — `movq $sym,8(%rsp)` (`imm32`, `R_X86_64_32S`), `cmpq $gtab+64,%rax`, `addl $sym,%eax` (not the `imm8` form), `pushl $sym`: GNU.  After: 190.
- [ ] **AS-T-196** If a relocation would not fit within the field its fixup names, then the assembler shall fail.
  Trace: AS-OBJ-002.  Verify: I — no code clamps a relocation's width to the instruction's length; T — no input of the suite writes a relocation over an opcode byte.  After: 191.
- [ ] **AS-T-197** When an instruction holds two symbolic fields, the assembler shall emit a relocation for each.
  Trace: AS-OBJ-015.  Verify: T — `movl $a,b`, `cmpl $a,b(%ebx)`: GNU.  After: 191.
- [ ] **AS-T-198** When a 64-bit instruction refers to an absolute symbol through a 32-bit displacement, the assembler shall emit `R_X86_64_32S` on that displacement.
  Trace: AS-EXT-004 (note 7).  Verify: T — `pandn foo,%xmm2`, `popcnt foo,%ebx`, `xsave foo`, `vpunpckldq foo,%ymm2,%ymm3`: GNU.  After: 191.
- [ ] **AS-T-199** When a memory operand of any x86 instruction is written with a segment override, the assembler shall emit the segment prefix.
  Trace: AS-EXT-004 (note 9).  Verify: T — `pshufb %fs:(%rax),%xmm2`; one instruction of each of SSE3, SSSE3, SSE4.1, SSE4.2, BMI1, BMI2, AVX, AVX2, FMA, AVX-512 with `%fs:` and `%gs:`: GNU.  After: 162.

### E.1 Relocation operators and types

- [ ] **AS-T-200** The assembler shall recognise a relocation operator as a suffix of a symbol in an expression, and shall not leave it in the symbol's name.
  Trace: AS-OBJ-003, AS-DES-003.  Verify: I — no `strstr` on symbol names for `@`; T — no symbol with `@` in its name reaches a symbol table but `.symver`'s.  After: 094.
- [ ] **AS-T-201** If a relocation operator is unknown, or not valid for the target or the field it is used in, then the assembler shall fail.
  Trace: AS-OBJ-003.  Verify: T — `foo@BOGUS`, `foo@GOTPCREL` under `--32`, `foo@GOTOFF` in a branch.  After: 200.
- [ ] **AS-T-202** When assembling for i386, the assembler shall emit for `@GOT`, `@GOTOFF`, `@PLT`, `@TLSGD`, `@TLSLDM`, `@DTPOFF`, `@GOTTPOFF`, `@GOTNTPOFF`, `@INDNTPOFF`, `@NTPOFF`, `@TPOFF`, `@TLSDESC` and `@TLSCALL` the relocation type the i386 psABI names.
  Trace: AS-OBJ-003.  Verify: T — one instruction and one data directive for each, against GNU; D — a PIC shared object and a program using each TLS model, built with `gcc -m32 -fPIC -S` and this assembler, link and run.  After: 200.
- [ ] **AS-T-203** When assembling for i386 and `_GLOBAL_OFFSET_TABLE_` is an operand, the assembler shall emit `R_386_GOTPC`.
  Trace: AS-OBJ-003.  Verify: T — `addl $_GLOBAL_OFFSET_TABLE_,%ebx` and the `+[.-.L1]` form.  After: 200.
- [ ] **AS-T-204** When assembling for x86-64, the assembler shall emit for `@GOT`, `@GOTOFF`, `@GOTPCREL`, `@PLT`, `@TLSGD`, `@TLSLD`, `@DTPOFF`, `@GOTTPOFF`, `@TPOFF`, `@TLSDESC`, `@TLSCALL`, `@SIZE` and `@PLTOFF` the relocation type the x86-64 psABI names.
  Trace: AS-OBJ-003.  Verify: T and D as 202, with `-m64`.  After: 200.
- [ ] **AS-T-205** When a `mov`, `test`, `binop`, `call` or `jmp` uses `@GOTPCREL`, the assembler shall emit `R_X86_64_GOTPCRELX` or `R_X86_64_REX_GOTPCRELX` as the psABI gives.
  Trace: AS-OBJ-015.  Verify: T — each instruction class with and without `REX`.  After: 204.
- [ ] **AS-T-206** When an i386 `call` or `jmp` names a symbol without `@PLT`, the assembler shall emit `R_386_PC32`.
  Trace: AS-OBJ-015.  Verify: T — global, local and undefined targets: GNU.  After: 191.
- [ ] **AS-T-207** When an x86-64 `call`, `jmp` or `jcc` names a global or undefined symbol, the assembler shall emit `R_X86_64_PLT32`.
  Trace: AS-OBJ-015.  Verify: T: GNU.  After: 191.
- [ ] **AS-T-208** When a 32-bit immediate of a 32-bit operation holds a symbol in 64-bit mode, the assembler shall emit `R_X86_64_32`, and `R_X86_64_32S` where the processor sign-extends the field.
  Trace: AS-OBJ-015.  Verify: T — `movl $sym,%eax` (32), `movq $sym,%rax` (32S), `addq $sym,%rax` (32S): GNU.  After: 191.
- [ ] **AS-T-209** When a conditional branch names an undefined symbol, the assembler shall encode the near form with a 32-bit relocation.
  Trace: AS-OBJ-015.  Verify: T — `je .Lundefined`, `je extern_sym`: GNU.  After: 191.
- [ ] **AS-T-210** When a data directive holds a symbol, the assembler shall emit a relocation of the directive's width.
  Trace: AS-OBJ-006.  Verify: T — `--64` `.long ext` (`R_X86_64_32`), `.word ext` (16), `.byte ext` (8), `.quad ext` (64); `--32` `.word ext` (`R_386_16`), `.byte ext` (`R_386_8`); the PC-relative forms of each.
- [ ] **AS-T-211** When a data directive holds `symbol - .` plus a constant, the assembler shall emit a PC-relative relocation with that addend.
  Trace: AS-OBJ-007, AS-FE-006.  Verify: T — `.long ext - .`, `.long ext+4 - .`, `.quad ext - .`, `.long x - .` for local x: GNU.
- [ ] **AS-T-212** When a data directive holds the difference of two symbols of one section, the assembler shall emit the constant.
  Trace: AS-OBJ-007, AS-FE-006.  Verify: T — `.long b-a`, `.long x-y+4`, `.quad b-a`, in the same and in another section: GNU.  After: 220.
- [ ] **AS-T-213** If an expression in a data directive or an operand cannot be expressed as a constant or as one relocation, then the assembler shall fail and say why.
  Trace: AS-OBJ-007, AS-FE-006.  Verify: T — `.long x+y`, `.long 2*x`, `.long ext - h` (h in another section), `movl $x+y,%eax`, `movl $2*x,%eax`: exit 1.  After: 191.
- [ ] **AS-T-214** When `.` is used in an expression, the assembler shall give the address of the start of the statement, or for a data directive of the element.
  Trace: AS-FE-006.  Verify: T — `.long .+4`, `.long ., .`, `jmp .`, `jmp .+2`, `movl $., %eax`: GNU; no symbol named `.` reaches the symbol table.  After: 220.
- [ ] **AS-T-215** When a reference to a local label lies in a mergeable section (`SHF_MERGE`), the assembler shall emit the relocation against the label's own symbol and not against the section.
  Trace: AS-OBJ-004.  Verify: T — three strings in `.rodata.str1.1,"aMS"` reached by `leaq .LCn(%rip)`: GNU's relocations; D — linked with GNU `ld`, the program prints `alpha|beta|gamma`.
- [ ] **AS-T-216** `as_x86_reloc.c` shall be the one place a relocation type is chosen, or shall be removed.
  Trace: AS-X86-002.  Verify: I.  After: 200.

## F. One layout, computed once

- [ ] **AS-T-220** The assembler shall hold, for each statement, its section, its offset, its bytes and its fixups, computed once.
  Trace: AS-DES-002.  Verify: I — the five passes that re-encode, and the "virtual layout" machinery, are gone.  After: 190.
- [ ] **AS-T-221** The assembler shall size branches by relaxation over that table until no size changes.
  Trace: AS-DES-002, AS-LAY-002.  Verify: T — the boundary cases at 126–129 bytes forward and 125–128 back, with alignment and `.org` between.  After: 220.
- [ ] **AS-T-222** When the distance between two places is computed, the assembler shall include the alignment padding between them.
  Trace: AS-LAY-001.  Verify: T — `jmp 1f` / `.skip 120,0x90` / `.p2align 4,0xcc` / `1: ret` is `eb 7e`; the backward case; `--64`; `a: .byte 1` / `.p2align 4` / `b: .byte 2` then `.long b-a` elsewhere is 16.  After: 220.
- [ ] **AS-T-223** When a branch crosses `.org`, the assembler shall reach the branch's label.
  Trace: AS-LAY-002.  Verify: T — `jmp 1f` / `.skip 60` / `.org 0x90` / `1: ret`.  After: 221.
- [ ] **AS-T-224** When a difference of labels spans a branch, the assembler shall use the branch's final size.
  Trace: AS-LAY-002.  Verify: T — `a: jmp 9f` / `.skip 200` / `9: b: ret` with `.long b-a` in and out of the section (0xce) and `.skip b-a` (206 bytes).  After: 221.
- [ ] **AS-T-225** If `.org` would move the location counter backwards, then the assembler shall fail.
  Trace: AS-LAY-002.  Verify: T.  After: 220.
- [ ] **AS-T-226** A reference to a local label shall resolve to the right definition whatever the number of local labels in the file.
  Trace: AS-OBJ-012.  Verify: T — 9,000 lines of `1: .long 1b - .`: every entry right; no fixed-size array of labels remains (I).  After: 220.
- [ ] **AS-T-227** The assembler shall look up symbols and sections through hashed indexes.
  Trace: AS-PERF-001, AS-PERF-003.  Verify: I — no linear scan by name per statement (`find_as_symbol_const`, `elf_find_section` through `section_for_name`, `sec_buf_find`, `as_sections.c:182-206`).  After: 220.
- [ ] **AS-T-228** Assembly time shall be linear in the number of statements, symbols, relocations and sections.
  Trace: AS-PERF-001, AS-PERF-003.  Verify: T — a timing test: each of the audit's five shapes (plain instructions, `movl extN`, `.long extN`, branches to `.L` labels, one section per function) and `dN: .long dN - dN-1` at n and 4n takes no more than 5 times as long at 4n; D — each within 5 times GNU's time on the reference host.  After: 220, 227.
- [ ] **AS-T-229** The assembler shall use no more than 200 bytes of memory for each source statement, on average over a file of 200,000 instructions.
  Trace: AS-PERF-001.  Verify: D — peak resident size under 60 MB for the audit's 200,000-line input (was 184 MB; GNU 12 MB).  After: 220.
- [ ] **AS-T-230** `as_relax.c` shall be the relaxation pass the assembler uses, or shall be removed.
  Trace: AS-DES-005, AS-DES-003.  Verify: I; `usr.bin/as/ARCHITECTURE.md` agrees.  After: 221.
- [ ] **AS-T-231** The program `as_data.c` builds shall be what the emitter emits from, or the module shall be reduced to what is used.
  Trace: AS-DES-005, AS-DES-003.  Verify: I — `emit_data_program` does not begin `(void)data`.  After: 220.
- [ ] **AS-T-232** The assembler shall have one emitter of each data directive.
  Trace: AS-DES-003.  Verify: I — the three emitters and four width ladders of AS-DES-003 are one.  After: 220.
- [ ] **AS-T-233** The assembler shall have one section state machine.
  Trace: AS-DES-005.  Verify: I — `section_track_*` in the emitter and `as_sections.c` are one.  After: 220.
- [ ] **AS-T-234** The helper functions `trim_copy`, `xstrdup`, `set_err`, `streq_ci` and the ELF constants shall each be defined once.
  Trace: AS-DES-005, AS-DES-006.  Verify: I — `grep -c` of each definition under `usr.bin/as` is 1; the constants come from `<elf.h>`.
- [ ] **AS-T-235** `as_elf_emit.c` shall hold the object writer and nothing else; the flat-binary writer, layout and symbol resolution shall each be in a file named for it.
  Trace: AS-DES-003.  Verify: I.  After: 164, 220.

## G. Sections, symbols and data

### G.1 Sections

- [ ] **AS-T-240** When a subsection number is given by `.text n`, `.data n`, `.section name` with `.subsection n` or `.pushsection name, n`, the assembler shall place the content in that subsection and emit each section's subsections in ascending order as one section.
  Trace: AS-SEC-002.  Verify: T — content in order 0, 1, 2 whatever the source order; one ELF `.text`; an alignment in a subsection applies to the section.  After: 024.
- [ ] **AS-T-241** When `.popsection` is assembled, the assembler shall restore both the current and the previous section saved by the matching `.pushsection`.
  Trace: AS-SEC-003.  Verify: T — `.previous` after a pop.
- [ ] **AS-T-242** When `.popsection` is assembled with nothing pushed, the assembler shall warn and continue.
  Trace: AS-SEC-003.  Verify: T — as GNU.
- [ ] **AS-T-243** When `.section` names a group with the `G` flag, the assembler shall take the group name and linkage from the arguments that follow the entry size where there is one.
  Trace: AS-SEC-004.  Verify: T — `"aMSG",@progbits,1,grp,comdat`; `"axG",@progbits,grp,comdat`.
- [ ] **AS-T-244** When two or more sections name one group, the assembler shall emit one group section with each as a member and its signature symbol once.
  Trace: AS-SEC-004.  Verify: T — against `readelf -g` of GNU's object.
- [ ] **AS-T-245** If a section flag character or section type is not known, then the assembler shall fail.
  Trace: AS-SEC-005.  Verify: T — `"aq"`, `@bogus`; `@init_array`, `@fini_array`, `@preinit_array`, `@note`, `@nobits`, `@unwind` and a numeric type are accepted with the right type.
- [ ] **AS-T-246** If a section is declared `M` without an entry size, or `G` without a group name, then the assembler shall fail.
  Trace: AS-SEC-005.  Verify: T.
- [ ] **AS-T-247** If a section is declared a second time with different flags, type or entry size, then the assembler shall fail, and shall leave the first declaration's attributes.
  Trace: AS-SEC-005.  Verify: T — as GNU ("changed section attributes").
- [ ] **AS-T-248** The assembler shall have one validator of alignment arguments, which accepts an alignment of 0 as 1 and any power of two the object format can hold.
  Trace: AS-SEC-006.  Verify: I — the three validators are one; T — `.align 0`, `.p2align 0`, `.balign 1`, the largest value, one more refused.
- [ ] **AS-T-249** When the argument of an alignment directive, `.zero`, `.skip`, `.space`, `.fill` or `.org` names symbols with absolute values, the assembler shall evaluate it.
  Trace: AS-SEC-006, AS-DAT-002, AS-OBJ-007.  Verify: T — `.set A,4` / `.align A`; `.set e1,0` / `.set e2,3` / `.zero e2-e1`; `.skip K*2`; `.fill K,1,0x66`; `.org K`.
- [ ] **AS-T-250** If the count of `.zero`, `.skip`, `.space` or `.fill` has no absolute value, or is negative, then the assembler shall fail.
  Trace: AS-OBJ-007, AS-DAT-002.  Verify: T — a count naming an undefined symbol; `.skip -1`; the special case for text holding `0b`, `-` and `.` is gone (I).
- [ ] **AS-T-251** When padding is emitted in a section that holds code and no fill is given, the assembler shall pad with the multi-byte NOP sequences GNU `as` uses for the mode.
  Trace: AS-SEC-006.  Verify: T — pads of 1 to 15 bytes in 32- and 64-bit modes: GNU.  After: 029.
- [ ] **AS-T-252** The assembler shall give a section the largest alignment asked for within it, and no more.
  Trace: AS-SEC-006.  Verify: T — a `.text` with no alignment directive has alignment 1; `.data` with `.balign 8` has 8: GNU.
- [ ] **AS-T-253** The assembler shall emit `.text`, `.data` and `.bss` always and any other section only when the source names it.
  Trace: AS-SEC-006.  Verify: T — no empty `.rodata` in the object of an empty source.
- [ ] **AS-T-254** The assembler shall accept section, symbol and file names of any length.
  Trace: AS-OBJ-021.  Verify: T — a 301-character and a 5,000-character section name, a relocation into each; no fixed buffer holds a name (I).
- [ ] **AS-T-255** When `.zero`, `.space`, `.skip` or `.fill` asks for more than 1 MiB, the assembler shall emit it.
  Trace: AS-OBJ-016.  Verify: T — `.zero 0x4000000` in `.data` and in `.bss`.

### G.2 Symbols

- [ ] **AS-T-260** When an absolute symbol is used as an immediate or displacement, the assembler shall encode its value with no relocation.
  Trace: AS-SYM-001.  Verify: T — `.set g1,2*8` / `movl $g1,%eax` is `b8 10 00 00 00`; `movl g1(%ebx),%eax`; a forward reference to a later `.set`.  After: 220.
- [ ] **AS-T-261** When `.set` is given `symbol ± constant`, the assembler shall define the symbol in the target's section at the target's value plus the constant.
  Trace: AS-SYM-001, AS-SYM-002.  Verify: T — `.set f1,4+c1` / `.long f1`: a relocation with addend 4 (today the 4 is lost).  After: 220.
- [ ] **AS-T-262** When `.set` is given the difference of two symbols of one section, the assembler shall define the symbol absolute with that difference.
  Trace: AS-SYM-001.  Verify: T — `.set j1, e1 - c1` / `.long j1`; `.set len, . - start`.  After: 220.
- [ ] **AS-T-263** When a symbol is an alias of another, the assembler shall resolve it whatever the order of definition.
  Trace: AS-SYM-002.  Verify: T — `.set a1,b1` / `.set b1,c1` / `c1: nop`: all three defined at one place.  After: 220.
- [ ] **AS-T-264** If aliases form a loop, then the assembler shall fail and name a symbol of the loop.
  Trace: AS-SYM-002.  Verify: T — `.set x,y` / `.set y,x`.
- [ ] **AS-T-265** When a symbol is an alias of an undefined symbol, the assembler shall relocate references to it against the undefined symbol.
  Trace: AS-SYM-002.  Verify: T — `.set a,ext` / `call a`: GNU.
- [ ] **AS-T-266** When a symbol is made an alias of a function or object by `.set` or `=`, the assembler shall give it the target's type.
  Trace: AS-SYM-002, AS-OBJ-011.  Verify: T — against `readelf -s` of GNU's object.
- [ ] **AS-T-267** If the expression of `.size` is not a constant or a difference of symbols of one section plus a constant, then the assembler shall fail.
  Trace: AS-SYM-003.  Verify: T — `.size f, e` and `.size f, e-g` with g in another section are refused; `.size f, e-g` in one section is the difference (today 3 for 2); the lone-symbol reading is kept only for a symbol set by `. - f`.  After: 220.
- [ ] **AS-T-268** When `.type` names `@gnu_indirect_function`, `@gnu_unique_object`, `@tls_object`, `@common`, `@notype`, `@function` or `@object`, in any of the spellings GNU accepts (`@`, `%`, quoted, `STT_*`), the assembler shall give the symbol that type.
  Trace: AS-SYM-004, AS-OBJ-011.  Verify: T — each.
- [ ] **AS-T-269** If `.type` names a type that is not known, then the assembler shall fail.
  Trace: AS-SYM-004.  Verify: T.
- [ ] **AS-T-270** When a symbol is named only by `.type`, `.size`, `.hidden`, `.weak` or the like and is never defined, the assembler shall emit it as an undefined global or weak symbol, or not at all where GNU emits none.
  Trace: AS-SYM-005.  Verify: T — no object of the suite holds a local undefined symbol; each case against GNU.
- [ ] **AS-T-271** When `.globl` or `.weak` names a symbol whose name begins `.L`, the assembler shall emit the symbol.
  Trace: AS-SYM-005.  Verify: T.
- [ ] **AS-T-272** When a symbol is assigned by `=` or `.set` more than once, the assembler shall give each use the value in force at that point and emit the last.
  Trace: AS-SYM-005.  Verify: T — `x = 6` / `.long x` / `x = x + 1` / `.long x` is 6, 7 and `x` is 7 in the symbol table.
- [ ] **AS-T-273** If a label is defined for a name already assigned by `.set`, or `.equiv` names a symbol already defined, then the assembler shall fail; a `.set` of a name after its label shall fail likewise.
  Trace: AS-SYM-005.  Verify: T — each against GNU's verdict.
- [ ] **AS-T-274** When `.comm name, size, align` is assembled, the assembler shall emit a common symbol with `st_size` the size and `st_value` the alignment.
  Trace: AS-OBJ-005.  Verify: T — `.comm bufa,64,32`; the default alignment; D — two objects with one `.comm` each link to one `.bss` object.
- [ ] **AS-T-275** When `.lcomm` is assembled, or `.comm` for a symbol declared `.local`, the assembler shall allocate the symbol in `.bss` as a local with its size and alignment.
  Trace: AS-OBJ-005.  Verify: T — against GNU; D — a linked program has distinct addresses for two such symbols.
- [ ] **AS-T-276** When `.symver` is assembled, the assembler shall emit the versioned symbol in the three forms `name@ver`, `name@@ver` and `name@@@ver`.
  Trace: AS-OBJ-011.  Verify: T — against `readelf -s` of GNU's object; D — a shared library built with a version script exports the versions.
- [ ] **AS-T-277** When `.file "name"` is assembled, the assembler shall emit an `STT_FILE` symbol of that name first in the symbol table.
  Trace: AS-OBJ-011.  Verify: T.
- [ ] **AS-T-278** When `.ident "text"` is assembled, the assembler shall append the text to `.comment`.
  Trace: AS-OBJ-011.  Verify: T.
- [ ] **AS-T-279** If a symbol is defined but has no recorded section and offset, then the assembler shall fail as an internal error.
  Trace: AS-OBJ-011.  Verify: I — the fallback to `.text+0` is gone.
- [ ] **AS-T-280** Every path of the symbol writer shall free what it allocated and check each result it is given.
  Trace: AS-OBJ-021, AS-ROB-001.  Verify: T — LeakSanitizer over the failing cases; I — the ignored result of `elf_symbol_define`, the count before the allocation check in `as_symtab.c` and the unchecked multiplication in `as_sections.c` are mended.

### G.3 Data

- [ ] **AS-T-281** When `.float` or `.double` is given a value out of range, the assembler shall emit what GNU emits and warn.
  Trace: AS-OBJ-016.  Verify: T — `.float 1e39`, a denormal, `inf`, `nan`.
- [ ] **AS-T-282** When a floating-point directive is given an integer in hexadecimal, the assembler shall take it as the bit pattern, as GNU does; when given `0f…`, `0d…` or `0e…`, as a decimal float.
  Trace: AS-DAT-003.  Verify: T — `.float 0x3fc00000`, `.float 0f1.5`, `.double 0d2.5`.
- [ ] **AS-T-283** The string escapes of `.ascii`, `.asciz` and `.string` shall be GNU `as`'s.
  Trace: AS-DAT-003.  Verify: T — `\a`, `\xg`, `\8`, `\x41`, `\101`, `\"`, `\\`, a trailing backslash.

## H. The object file

- [ ] **AS-T-290** When `.cfi_startproc` … `.cfi_endproc` and the `.cfi_*` directives between them are assembled, the assembler shall emit `.eh_frame` with a CIE and an FDE that encode them, and the relocations of the FDE.
  Trace: AS-OBJ-010.  Verify: T — `readelf --debug-dump=frames` of our object equals GNU's for the output of `gcc -S` at `-O0` and `-O2`, both modes; D — a C++ program built with this assembler throws through and catches in such code.  After: 070, 220.
- [ ] **AS-T-291** Each of `.cfi_def_cfa`, `.cfi_def_cfa_register`, `.cfi_def_cfa_offset`, `.cfi_adjust_cfa_offset`, `.cfi_offset`, `.cfi_rel_offset`, `.cfi_register`, `.cfi_restore`, `.cfi_undefined`, `.cfi_same_value`, `.cfi_remember_state`, `.cfi_restore_state`, `.cfi_return_column`, `.cfi_signal_frame`, `.cfi_escape`, `.cfi_personality`, `.cfi_lsda`, `.cfi_sections` and `.cfi_window_save` shall be encoded or refused by name.
  Trace: AS-OBJ-010.  Verify: T — one case each.  After: 290.
- [ ] **AS-T-292** The assembler shall not emit `.eh_frame_hdr` in a relocatable object.
  Trace: AS-OBJ-010.  Verify: T.
- [ ] **AS-T-293** When `.cfi_sections .debug_frame` is given, the assembler shall emit `.debug_frame`.
  Trace: AS-OBJ-010.  Verify: T.  After: 290.
- [ ] **AS-T-294** When `.file N "name"` and `.loc` directives are assembled, the assembler shall emit a `.debug_line` program that `readelf --debug-dump=decodedline` reads as the same rows GNU's does.
  Trace: AS-OBJ-010.  Verify: T — the output of `gcc -g -S`.  After: 220.
- [ ] **AS-T-295** Where `-g` is given for an assembly source with no debug directives, the assembler shall emit line information for the source itself, or shall refuse the option.
  Trace: AS-OBJ-010, AS-FE-014.  Verify: T — `readelf --debug-dump=info` reports no corruption.
- [ ] **AS-T-296** The assembler shall write the output with the permissions `0666` less the umask.
  Trace: AS-OBJ-020, AS-FE-032.  Verify: T — umask 022 gives 0644.
- [ ] **AS-T-297** When the output path is `/dev/null` or another file that is not regular, the assembler shall write to it and succeed.
  Trace: AS-OBJ-020.  Verify: T — `-o /dev/null`; a FIFO.
- [ ] **AS-T-298** If the output path names the same file as an input, then the assembler shall fail before reading.
  Trace: AS-OBJ-020.  Verify: T — by path and by hard link.
- [ ] **AS-T-299** When the output path is a symbolic link, the assembler shall write to the link's target, as GNU does.
  Trace: AS-OBJ-020.  Verify: T — a dangling link and a live one.
- [ ] **AS-T-300** If the output cannot be written, then the message shall name the path and give the system's reason.
  Trace: AS-OBJ-020.  Verify: T — a read-only directory; a full device (`/dev/full`).
- [ ] **AS-T-301** The clean-up of `as_elf_emit_file` shall be written once.
  Trace: AS-DES-003.  Verify: I.

## I. The x86 extensions

After section D these are table rows; each is checked by the extension
corpus (`aud/ext/`), whose "wrong" and "accepts-invalid" classes must be
empty for the family named.

- [ ] **AS-T-310** When an AVX, AVX2, FMA, F16C or BMI instruction is written with XMM or YMM registers 0–15 and no mask, broadcast or rounding decorator, the assembler shall emit the VEX encoding.
  Trace: AS-EXT-002, AS-OBJ-014.  Verify: T — the 1,844 (32-bit) and 3,050 (v4) "EVEX where GNU uses VEX" lines are identical to GNU.  After: 161.
- [ ] **AS-T-311** When a VEX compare is written with a vector destination, the assembler shall encode the vector destination.
  Trace: AS-EXT-002.  Verify: T — `vpcmpeq{b,w,d,q}`, `vpcmpgt{b,w,d,q}`, `vcmp{ps,pd,ss,sd}` with XMM and YMM destinations: GNU.  After: 310.
- [ ] **AS-T-312** While no `-march` is given, the assembler shall accept every x86 instruction set it implements.
  Trace: AS-EXT-003.  Verify: T — `vaddps`, `andn`, `vfmadd231pd` under plain `--64`; the output of `gcc -mavx2 -S` assembles with no option.
- [ ] **AS-T-313** Where `-march` names a level, the assembler shall refuse instructions outside it and name the level they need.
  Trace: AS-EXT-003.  Verify: T — `vsqrtps %ymm1,%ymm2` at v3 is accepted (VEX), `%zmm` at v3 refused "requires x86-64-v4".  After: 167.
- [ ] **AS-T-314** When an F16C instruction is written, the assembler shall encode it.
  Trace: AS-EXT-003.  Verify: T — `vcvtph2ps`, `vcvtps2ph`, register and memory, XMM and YMM.  After: 161.
- [ ] **AS-T-315** When an AVX-512CD instruction is written, the assembler shall encode it.
  Trace: AS-EXT-003.  Verify: T — `vpconflict{d,q}`, `vplzcnt{d,q}`, `vpbroadcastm{b2q,w2d}`.  After: 161.
- [ ] **AS-T-316** If an operand, register or prefix is not valid in the mode being assembled, then the extension encoders shall refuse the instruction.
  Trace: AS-EXT-004 (note 5).  Verify: T — under `--32`: `addps %xmm9,%xmm1`, `vaddps %xmm9,%xmm1,%xmm2`, `haddps (%rax),%xmm1`, `andn %rax,%rbx,%rcx`, `pextrq $1,%xmm1,%rax`, `cvtsi2sdq %rax,%xmm1`: exit 1.  After: 171.
- [ ] **AS-T-317** When an SSSE3 instruction or `pextrw` is written with MMX registers, the assembler shall encode the MMX form without `66`.
  Trace: AS-EXT-005.  Verify: T — the sixteen SSSE3 mnemonics with `%mm` operands and memory; `pextrw $3,%mm1,%eax`.  After: 161.
- [ ] **AS-T-318** When an EVEX prefix is built, the assembler shall place ModRM.reg bit 3 in `R` and bit 4 in `R'`, and ModRM.rm bit 4 in `X` for a register operand.
  Trace: AS-EXT-006.  Verify: T — destinations and sources 8–15 and 16–31 for one instruction of each EVEX form.  After: 162.
- [ ] **AS-T-319** When an EVEX memory operand has a displacement, the assembler shall use the compressed 8-bit displacement where the displacement is a multiple of the instruction's N within range, and a 32-bit one otherwise.
  Trace: AS-EXT-006.  Verify: T — `vandpd 8(%rsp),…` with EVEX forced, `vaddps 64(%rax),%zmm1,%zmm2` (disp8 = 1), `vaddps 8(%rax),%zmm1,%zmm2` (disp32), broadcast, scalar, half- and quarter-vector tuples: GNU.  After: 162.
- [ ] **AS-T-320** When registers `%xmm16`–`31`, `%ymm16`–`31` or `%zmm16`–`31` are written in 64-bit mode, the assembler shall accept them and choose EVEX.
  Trace: AS-EXT-006.  Verify: T — `vaddps %zmm17,%zmm18,%zmm19`, `vmovaps %xmm16,%xmm1`.  After: 160.
- [ ] **AS-T-321** When a compare or test writes an opmask register, the assembler shall take the write mask from the `{%kN}` decorator alone.
  Trace: AS-EXT-007.  Verify: T — `vptestmw (%rax),%zmm2,%k1` (no mask), `vpcmpq $3,%zmm1,%zmm2,%k1{%k2}`; `vpcmp*`, `vcmp*`, `vptestm*`, `vptestnm*`, `vpshufbitqmb`.
- [ ] **AS-T-322** When a mask, zeroing or broadcast decorator is written on an instruction with an EVEX form, the assembler shall choose the EVEX form and encode the decorator.
  Trace: AS-EXT-007.  Verify: T — `vfmadd132ss %xmm1,%xmm2,%xmm3{%k1}`, `vsubss 0x44(%rax),%xmm2,%xmm3{%k1}`, `vpbroadcastb %xmm1,%xmm2{%k1}{z}`; the 1,467 mask-dropped lines of the v3 corpus.  After: 310.
- [ ] **AS-T-323** If a decorator is written on an instruction with no EVEX form, or `{z}` without a mask, or two masks, or a broadcast count that does not match the vector length and element size, then the assembler shall refuse the instruction.
  Trace: AS-EXT-007.  Verify: T — `addss (%rax){1to8},%xmm2`, `psllw (%rax),%xmm2{%k1}`, `vaddps %zmm1,%zmm2,%zmm3{z}`, `{%k1}{%k2}`, `vaddps (%rax){1to3},…`, `vaddps (%rax){1to8},%zmm1,%zmm2`.
- [ ] **AS-T-324** When a broadcast decorator is written, the assembler shall encode the count written.
  Trace: AS-EXT-007.  Verify: T — `vcvtpd2udq (%rax){1to4},%xmm2`, each `{1toN}` with each vector length.
- [ ] **AS-T-325** When `{rn-sae}`, `{rd-sae}`, `{ru-sae}`, `{rz-sae}` or `{sae}` is written, the assembler shall encode the embedded rounding or exception suppression.
  Trace: AS-EXT-007, AS-EXT-009.  Verify: T — `vaddps {rn-sae},%zmm1,%zmm2,%zmm3`, `vcmpps $1,{sae},%zmm1,%zmm2,%k1`, `vcvtsd2si {rz-sae},%xmm1,%eax`.
- [ ] **AS-T-326** When a four-operand VEX instruction names a register in its immediate byte, the assembler shall place the register's four bits in bits 7–4 of the byte.
  Trace: AS-EXT-008.  Verify: T — `vblendvps`, `vblendvpd`, `vpblendvb` with registers 0–15 in the fourth position; the twenty FMA4 mnemonics.
- [ ] **AS-T-327** When `vpermilps` or `vpermilpd` is written, the assembler shall encode the variable form in map `0F38` and the immediate form in map `0F3A`.
  Trace: AS-EXT-008.  Verify: T — both forms, XMM and YMM.
- [ ] **AS-T-328** When a VEX instruction's template gives `W = 1`, the assembler shall set it.
  Trace: AS-EXT-008.  Verify: T — `vpinsrq`, `vpextrq`, `vmovq` with general registers, `vpmadd52luq`, `vpmadd52huq`, `vgf2p8affineqb`, `vgf2p8affineinvqb`.  (`vcvtsi2sdq` is in 066.)
- [ ] **AS-T-329** When `vprol`, `vpror`, `vprolv` or `vprorv` (`d` and `q`) is written, the assembler shall place the destination in `vvvv` and the opcode extension in ModRM.reg for the immediate forms.
  Trace: AS-EXT-008.  Verify: T — `vprord $3,%zmm1,%zmm2{%k1}` and the others.
- [ ] **AS-T-330** When an instruction's source and destination vectors differ in length, the assembler shall take the vector length from the operand the manual names.
  Trace: AS-EXT-008.  Verify: T — `vcvtqq2ps (%rax),%ymm2`, `vcvtps2qq %ymm1,%zmm2`, `vcvtpd2ps`, `vcvtps2pd`, `vinsertf32x8`, `vinserti32x8`, `vinsertf64x4`, `vinserti64x4`, `vextract*`, with the `x`/`y`/`z` suffixes for memory sources.
- [ ] **AS-T-331** When `vpbroadcast{b,w,d,q}` is written with a general register source, the assembler shall encode the EVEX general-register form; with a ZMM destination and memory source, the EVEX form.
  Trace: AS-EXT-008.  Verify: T — `vpbroadcastb %eax,%ymm1`, `vpbroadcastq %rax,%zmm1`, `vpbroadcastd (%rax),%zmm2`.
- [ ] **AS-T-332** When any of these AVX instructions is written, the assembler shall encode it: `vmovaps`, `vmovups`, `vmovapd`, `vmovupd`, `vmovdqa`, `vmovdqu`, `vmovd`, `vmovq`, `vmovss`, `vmovsd`, `vmovlps`, `vmovhps`, `vmovlpd`, `vmovhpd` (register, load and store forms), `vmovnt*`, `vmovmskp*`, `vpmovmskb`, `vsqrtp[sd]`, `vrcpps`, `vrsqrtps`, the `vcvt*` family, `vcomis*`, `vucomis*`, `vpshufd`, `vpshufhw`, `vpshuflw`, `vpabs*`, `vpmovsx*`, `vpmovzx*`, `vptest`, `vpextr*`, `vextractps`, `vroundp[sd]`, `vrounds[sd]`, `vlddqu`, `vldmxcsr`, `vstmxcsr`, `vpcmp[ei]str*`, `vphminposuw`, the `vcmpeqps`-style predicate aliases, `vpslldq`, `vpsrldq`.
  Trace: AS-EXT-009.  Verify: T — the 88 mnemonics with no row: the `avx` family of the corpus shows no "refused".  After: 161.
- [ ] **AS-T-333** When `vextractf128`, `vextracti128`, `vpermq`, `vpermpd` (immediate) or `rorx` is written, the assembler shall encode it.
  Trace: AS-EXT-009.  Verify: T — each, register and memory.
- [ ] **AS-T-334** When a gather or scatter is written with a vector index, the assembler shall encode it.
  Trace: AS-EXT-009, AS-EXT-008 (note 18d).  Verify: T — the AVX2 gathers with XMM and YMM index (including `vgatherqps`/`vpgatherqd` with a YMM index), the AVX-512 gathers and scatters.  After: 177.
- [ ] **AS-T-335** When an AES-NI, VAES, PCLMULQDQ, VPCLMULQDQ, SHA or GFNI instruction is written, the assembler shall encode it in 32- and 64-bit modes.
  Trace: AS-EXT-009.  Verify: T — `aesenc`, `aesenclast`, `aesdec`, `aesdeclast`, `aesimc`, `aeskeygenassist`, their VEX forms, `pclmulqdq` and its aliases, the seven SHA instructions, `gf2p8*`.
- [ ] **AS-T-336** When any of these AVX-512 instructions is written, the assembler shall encode it: `vcompressp[sd]`, `vpcompress[dq]`, `vexpandp[sd]`, `vpexpand[dq]`, `vpmovs*`, `vpmovus*`, `vpmov[wdq]*`, `vpabsq`, `vextract[fi]32x4`, `vextract[fi]64x4`, `vextract[fi]32x8`, `vextract[fi]64x2`, `vbroadcast[fi]32x4`, `vbroadcast[fi]64x4`, `vbroadcast[fi]64x2`, `vcvt*2usi`, `vcvttp[sd]2udq`, `vcvtudq2pd`.
  Trace: AS-EXT-009.  Verify: T — the 34 fully refused mnemonics.
- [ ] **AS-T-337** When `blsi`, `blsr`, `blsmsk` or `rorx` is written in 32-bit mode, or `xrstors`/`xrstors64` in their modes, the assembler shall encode it.
  Trace: AS-EXT-009.  Verify: T.
- [ ] **AS-T-338** When `movbe` is written with two registers, the assembler shall refuse it.
  Trace: AS-EXT-010.  Verify: T — `movbe %eax,%ebx`: exit 1 (GNU accepts it only with APX).
- [ ] **AS-T-339** The extension corpus shall show no line in the classes "wrong" and "accepts-invalid" for any family.
  Trace: AS-EXT-001 to 010.  Verify: T — the differential harness over `aud/ext/` in the three modes with an empty baseline for those classes.  After: 310–338.

## J. Modes

- [ ] **AS-T-345** When `.code16`, `.code32` or `.code64` is assembled, the assembler shall encode what follows for that mode, whatever `--32` or `--64` said.
  Trace: AS-X86-030.  Verify: T — `--64` with `.code32` then `movl %eax,%ebx`, `.code16` then the same (`66 89 c3`).  After: 161.
- [ ] **AS-T-346** If `.code64` is assembled into an ELF32 object, or `.code16`/`.code32` instructions need a relocation the object format lacks, then the assembler shall fail.
  Trace: AS-X86-030.  Verify: T — against GNU's verdict for each.
- [ ] **AS-T-347** While assembling 16-bit code, the assembler shall emit `66` for 32-bit operands and `67` for 32-bit addressing, and 16-bit displacements, immediates and branch offsets otherwise.
  Trace: AS-X86-030.  Verify: T — a corpus of the core instructions under `.code16`: `push %eax`, `xor %eax,%eax`, `movw sym,%ax` (`R_386_16`), `lgdt sym`, `callw sym`, `je sym` (`0f 84 rel16`), `jmp far`, `ljmp $seg,$off`: GNU; D — a boot sector and a real-mode trampoline assembled by both assemblers are byte-identical.  After: 345, 176.
- [ ] **AS-T-348** When `.code16gcc` is assembled, the assembler shall encode stack and branch instructions with 32-bit operand size, as GNU does.
  Trace: AS-X86-030.  Verify: T — `call`, `ret`, `push`, `pop`, `enter`, `leave` under `.code16gcc`.  After: 347.
- [ ] **AS-T-349** If an instruction, register or suffix valid only in 64-bit mode is written in 16- or 32-bit mode, then the assembler shall refuse it.
  Trace: AS-X86-030, AS-OBJ-013.  Verify: T — `pushq %rax`, `incq`, `retq`, `leaveq`, `callq`, `jmpq *%eax`, `stosq`, `rex movb`, `in $0x60,%rax`, `addq $1,%rax`, `movq %rax,%rbx`, `%r8`, `%sil`, `syscall` is accepted, `swapgs` refused.  After: 161.
- [ ] **AS-T-350** If an instruction or operand size not valid in 64-bit mode is written in 64-bit mode, then the assembler shall refuse it.
  Trace: AS-X86-030.  Verify: T — `push %eax`, `pop %ebx`, `into`, `salc`, `pushfl`, `popfl`, `retl`, `jmp *%eax`, `calll *(%rax)`, `pusha`, `aaa`, `daa`, `bound`, `les`, `lds`, `inc %eax` uses `FF /0` not `40`.  After: 161.

## K. Documents and dead code

- [ ] **AS-T-355** `usr.bin/as/ARCHITECTURE.md` shall describe the files, passes and tests that exist.
  Trace: AS-DOC-001.  Verify: I — every file and test it names exists; every source file is named.  After: 235.
- [ ] **AS-T-356** `--target-help` and `--help` shall list the targets and options the assembler implements, and no others.
  Trace: AS-DOC-001, AS-ARM-001.  Verify: T — each option listed is accepted and each accepted is listed.
- [ ] **AS-T-357** `docs/specs/as_spec.md` shall state what the assembler does, or each divergence shall be listed in it as open.
  Trace: AS-DOC-002.  Verify: I — a line-by-line reconciliation recorded in the commit.
- [ ] **AS-T-358** `as(1)` (`usr.man/man1/as.1`) shall describe every option, the directives implemented and the limits, and shall pass `mandoc -T lint`.
  Trace: AS-DOC-002.  Verify: I; T — `mandoc -T lint`.
- [ ] **AS-T-359** `TASKLIST_AS.md` shall be closed out: each of its 24 open items either ticked, moved here with a number, or struck as obsolete.
  Trace: AS-TST-002.  Verify: I.
- [ ] **AS-T-360** The assembler's sources shall hold no function that nothing calls.
  Trace: AS-FE-052, AS-DES-003, AS-DES-004.  Verify: A — built with `-ffunction-sections -Wl,--gc-sections -Wl,--print-gc-sections`, no function of `usr.bin/as` is discarded; the dead code named in AS-DES-003 and AS-DES-004 is gone.

## L. ARM and AArch64

Section B removes the names (083, 084).  This section is the alternative:
make the targets real.  It is last because nothing else depends on it and
the seventeen files are, as they stand, fixtures and not encoders.  If it
is not to be done, 370 is ticked with "removed" and the rest struck.

- [ ] **AS-T-370** A decision shall be recorded in `usr.bin/as/ARCHITECTURE.md`: the ARM and AArch64 back ends are to be completed, or their seventeen files are removed.
  Trace: AS-ARM-001, AS-ARM-004.  Verify: I.
- [ ] **AS-T-371** When invoked as `arm-as` or `aarch64-as`, or with `--target` naming one, the assembler shall parse for that architecture and write an object of that machine type.
  Trace: AS-ARM-001, AS-ARM-002.  Verify: T — `add r0, r0, r1` under `arm-as` gives an `EM_ARM` object holding `e0800001`; `ret` under `aarch64-as` an `EM_AARCH64` object holding `d65f03c0`.  After: 370, 090.
- [ ] **AS-T-372** The ARM and AArch64 instruction structures shall hold the operands of the instruction.
  Trace: AS-ARM-004.  Verify: I — no encoder is a table of one fixed word per mnemonic.  After: 371.
- [ ] **AS-T-373** When an ARM or AArch64 instruction is encoded, the encoding shall be of the operands written.
  Trace: AS-ARM-004.  Verify: T — a differential corpus against `llvm-mc` for each instruction class (data processing, multiply, load/store, load/store multiple, branch, system, VFP, NEON; AArch64 data processing immediate and register, load/store, branch, system, SIMD): identical words.  After: 372.
- [ ] **AS-T-374** If an ARM or AArch64 operand cannot be encoded, then the assembler shall fail and say why.
  Trace: AS-ARM-005, AS-ARM-008.  Verify: T — `add r0, r1, #0x101`, `mov r0, #0x12345` (without `movw`), `movw r3, #0x10000`, a branch out of range, a misaligned branch target: exit 1 with the specific message.  After: 372.
- [ ] **AS-T-375** When an ARM shift of `lsr #32`, `asr #32` or `rrx` is written, the assembler shall encode it; if `lsr #0`, `asr #0` or `ror #0` is written, it shall encode `lsl #0` or refuse as the architecture defines.
  Trace: AS-ARM-006.  Verify: T — against `llvm-mc`.
- [ ] **AS-T-376** If an ARM register number is out of range for its field, then the encoder shall refuse it.
  Trace: AS-ARM-006.  Verify: T — a unit test of each field.
- [ ] **AS-T-377** When `ldr rX, =const` is written, the assembler shall place the constant in a literal pool emitted at `.ltorg`/`.pool` or the end of the section, and load it PC-relative.
  Trace: AS-ARM-006.  Verify: T — against `llvm-mc`; D — the code runs under `qemu-arm`.
- [ ] **AS-T-378** When an ARM load or store has a negative offset, or post-indexing, the assembler shall encode the U and P bits accordingly.
  Trace: AS-ARM-006.  Verify: T — `ldr r0,[r1,#-4]`, `ldr r0,[r1],#4`, `str r0,[r1,#-4]!`.
- [ ] **AS-T-379** When `ldm` or `stm` is written with a stack suffix (`fd`, `ed`, `fa`, `ea`), the assembler shall map it for a load and for a store as the architecture defines.
  Trace: AS-ARM-006.  Verify: T — the eight combinations; `push`/`pop` with any register list.
- [ ] **AS-T-380** The assembler shall split an ARM mnemonic into base, `s` flag and condition by a table of the real mnemonics, in unified and divided order.
  Trace: AS-ARM-006.  Verify: T — `teq`, `svc`, `bls`, `mrs`, `movseq`, `moveqs`, `bleq`, `blt`, `blo`; the copy in `as_parser.c` is gone.
- [ ] **AS-T-381** When a conditional hint is written, the assembler shall encode its condition.
  Trace: AS-ARM-006.  Verify: T — `nopeq`, `wfine`.
- [ ] **AS-T-382** When Thumb `add`, `orn` and their fellows are encoded, the assembler shall choose flag-setting by the mnemonic and the IT state, and emit the two halves of a 32-bit instruction in one order.
  Trace: AS-ARM-006.  Verify: T — against `llvm-mc -mattr=+thumb2`.
- [ ] **AS-T-383** When AArch64 `ret` is written with no register, the assembler shall encode `ret x30`.
  Trace: AS-ARM-007.  Verify: T.
- [ ] **AS-T-384** The AArch64 branch encoder shall match the mnemonic before it validates fields, and shall accept `b.eq` and each condition by name.
  Trace: AS-ARM-007.  Verify: T — `cbz`, `b.ne`, `b.hs`, `bne` (alias).
- [ ] **AS-T-385** The AArch64 register parser shall accept `x0`–`x30`, `w0`–`w30`, `sp`, `wsp`, `xzr`, `wzr`, `fp`, `lr`, `ip0`, `ip1` and the vector and FP registers, and shall refuse `x05` and `x007`.
  Trace: AS-ARM-007.  Verify: T — a unit test.
- [ ] **AS-T-386** The AArch64 relocation table shall name only relocations the ELF ABI for AArch64 defines.
  Trace: AS-ARM-007.  Verify: I — `R_AARCH64_MOVW_UABS_G3_NC` is gone.
- [ ] **AS-T-387** When `.inst`, `.word`, `.arch`, `.syntax`, `.arm`, `.thumb`, `.cpu`, `.fpu`, `.eabi_attribute`, `.fnstart`, `.fnend`, `.thumb_func`, `.ltorg` or `.pool` is assembled for ARM or AArch64, the assembler shall give it its effect or fail.
  Trace: AS-ARM-002.  Verify: T — `.inst 0xd65f03c0` emits four bytes; `.word 0xe12fff1e` four; each other directive has a case.  After: 371.

## Trace: audit entry to tasks

| Audit entry | Tasks |
|---|---|
| AS-TST-001 | 010, 011, 014–019 |
| AS-TST-002 | 012, 013, 359 |
| AS-DOC-001 | 355, 356 |
| AS-DOC-002 | 357, 358 |
| AS-ARM-001 | 083, 356, 370, 371 |
| AS-ARM-002 | 084, 371, 387 |
| AS-ARM-003 | 082 |
| AS-ARM-004 | 370, 372, 373 |
| AS-ARM-005 | 374 |
| AS-ARM-006 | 375–382 |
| AS-ARM-007 | 383–386 |
| AS-ARM-008 | 166, 374 |
| AS-FE-001 | 123–128 |
| AS-FE-002 | 025, 026 |
| AS-FE-003 | 027 |
| AS-FE-004 | 134, 135 |
| AS-FE-005 | 028 |
| AS-FE-006 | 001, 211–214 |
| AS-FE-007 | 007, 103–106 |
| AS-FE-008 | 107, 109 |
| AS-FE-009 | 093 |
| AS-FE-010 | 074–077, 110, 111, 118 |
| AS-FE-011 | 130–132 |
| AS-FE-012 | 006, 112–117, 119 |
| AS-FE-013 | 099 |
| AS-FE-014 | 138–140, 142–145 |
| AS-FE-020 | 023 |
| AS-FE-021 | 002 |
| AS-FE-022 | 120 |
| AS-FE-023 | 121, 122 |
| AS-FE-024 | 091, 150, 151 |
| AS-FE-025 | 102, 152 |
| AS-FE-030 | 090, 097, 147 |
| AS-FE-031 | 108, 148 |
| AS-FE-032 | 146, 147, 149, 296 |
| AS-FE-040 | 033, 098, 100, 101, 140, 141 |
| AS-FE-050 | 090–092 |
| AS-FE-051 | 094–096 |
| AS-FE-052 | 153, 154, 360 |
| AS-OBJ-001 | 192, 193 |
| AS-OBJ-002 | 194–196 |
| AS-OBJ-003 | 200–204 |
| AS-OBJ-004 | 215 |
| AS-OBJ-005 | 274, 275 |
| AS-OBJ-006 | 210 |
| AS-OBJ-007 | 211–213, 249, 250 |
| AS-OBJ-008 | 067–074 |
| AS-OBJ-009 | 078–080 |
| AS-OBJ-010 | 290–295 |
| AS-OBJ-011 | 266, 268, 276–279 |
| AS-OBJ-012 | 226 |
| AS-OBJ-013 | 039, 040, 180, 349 |
| AS-OBJ-014 | 310 |
| AS-OBJ-015 | 197, 205–209 |
| AS-OBJ-016 | 009, 081, 119, 255, 281 |
| AS-OBJ-020 | 296–300 |
| AS-OBJ-021 | 254, 280 |
| AS-PERF-001 | 227–229 |
| AS-PERF-002 | 161, 165 |
| AS-PERF-003 | 227, 228 |
| AS-DES-001 | 190, 191 |
| AS-DES-002 | 220, 221 |
| AS-DES-003 | 164, 200, 232, 235, 301, 360 |
| AS-DES-004 | 096, 160–164, 360 |
| AS-DES-005 | 001, 230, 231, 233, 234 |
| AS-DES-006 | 161, 162, 166, 167, 234 |
| AS-SEL-001 | 030, 031 |
| AS-SEL-002 | 194 |
| AS-SEL-003 | 180 |
| AS-SEL-004 | 181, 182 |
| AS-SEL-005 | 174, 175 |
| AS-SEL-006 | 034, 066 |
| AS-SEL-007 | 169, 178, 179 |
| AS-SEL-008 | 185 |
| AS-SEL-009 | 029, 043, 049–053 |
| AS-SEL-010 | 133, 176, 177 |
| AS-SEL-011 | 171, 183 |
| AS-SEL-012 | 172, 173 |
| AS-SEL-013 | 003 |
| AS-SEL-014 | 036, 041, 042, 047, 048, 054 |
| AS-LAY-001 | 222 |
| AS-LAY-002 | 221, 223–225 |
| AS-LAY-003 | 134–137, 187 |
| AS-SEC-001 | 024 |
| AS-SEC-002 | 240 |
| AS-SEC-003 | 241, 242 |
| AS-SEC-004 | 243, 244 |
| AS-SEC-005 | 080, 081, 245–247 |
| AS-SEC-006 | 072, 248, 249, 251–253 |
| AS-SYM-001 | 004, 260–262 |
| AS-SYM-002 | 261, 263–266 |
| AS-SYM-003 | 008, 267 |
| AS-SYM-004 | 268, 269 |
| AS-SYM-005 | 270–273 |
| AS-DAT-001 | 005 |
| AS-DAT-002 | 249, 250 |
| AS-DAT-003 | 064, 129, 282, 283 |
| AS-ROB-001 | 001, 280 |
| AS-X86-001 | 020 |
| AS-X86-002 | 216 |
| AS-X86-003 | 163 |
| AS-X86-010 | 047, 160, 168–171 |
| AS-X86-011 | 035, 036 |
| AS-X86-012 | 061, 062 |
| AS-X86-013 | 063 |
| AS-X86-020 | 038 |
| AS-X86-021 | 039 |
| AS-X86-022 | 058–060 |
| AS-X86-023 | 037, 041–043 |
| AS-X86-024 | 034, 065, 066, 180 |
| AS-X86-025 | 055, 056 |
| AS-X86-026 | 032, 044–046, 137 |
| AS-X86-030 | 345–350 |
| AS-X86-040 | 022 |
| AS-X86-041 | 033, 054, 057, 172, 184 |
| AS-X86-042 | 186 |
| AS-EXT-001 | 020, 021 |
| AS-EXT-002 | 310, 311 |
| AS-EXT-003 | 312–315 |
| AS-EXT-004 | 162, 192, 194, 198, 199, 316 |
| AS-EXT-005 | 034, 317 |
| AS-EXT-006 | 318–320 |
| AS-EXT-007 | 321–325 |
| AS-EXT-008 | 052, 059, 066, 180, 185, 326–331 |
| AS-EXT-009 | 185, 325, 332–337 |
| AS-EXT-010 | 040, 171, 338, 339 (339 closes all of AS-EXT) |

332 tasks: 9 done, 323 open.  Numbers run to 387, with gaps left between
sections for tasks found along the way.
