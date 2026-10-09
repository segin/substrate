# Audit of the in-tree linker (`usr.bin/ld`) — requirements checklist

Audit of the hand-written linker restored in `3169d142b`, carried out
2026-10-08.  The linker is `usr.bin/ld/ld.c` (11,680 lines, 257 functions)
driving the ELF object library `usr.lib/elfobj/`, where sections, symbols
and relocations are actually merged (`src/elf_link.c`) and relocation
arithmetic is done (`src/elf_reloc.c`).  Line numbers are those of that
commit.

Every finding is written as the requirement the code does not meet, in
EARS form (INCOSE *Guide to Writing Requirements*): **ubiquitous** ("The
linker shall …"), **event-driven** ("When …, the linker shall …"),
**state-driven** ("While …"), **unwanted behaviour** ("If …, then the
linker shall …") and **optional feature** ("Where …").  A box is ticked when
the requirement is met and a test shows it.

- **Evidence** is where the code falls short and what happens.
- **Basis**: *verified* — checked against the code by hand after the review;
  *traced* — a reviewer followed the code path fully; *suspected* — plausible,
  not every path followed; *measured* — timed on a host build of the linker.
- **Task** is the entry in the working task list the item was filed under.

Nothing here has been fixed yet.  What the linker does correctly today:
static, non-PIC, non-TLS links on both architectures, and on x86-64 PIC
code limited to `PLT32` calls and `mov sym@GOTPCREL` loads.  Its layout
arithmetic is overflow-checked and its output is written atomically with
short writes and close errors checked.

## 1. Linking today's objects

- [x] **LD-CUR-001** When an input section is compressed (`SHF_COMPRESSED`), the linker shall either decompress it before merging or discard it together with its relocations.
  Met: `elf_link.c` merges a compressed section as its contents, inflated by `elf_inflate.c` (zlib only; another method is reported as unsupported).  Shown on a host build: the link against `libc.a` and `libsys.a` no longer fails in the merge; the inflate was checked against 30 streams (stored, fixed and dynamic blocks; empty to 200 KB) and rejects wrong lengths and truncation.
  Evidence: `elf_link.c:653` appends the compressed bytes; `.rel.debug_*` offsets then exceed the merged section (`elf_reloc.c:2751-2766`); there is no inflate in `elfobj`.  `libc.a` and `libsys.a` carry zlib-compressed `.debug_*` (the libraries are built by the host `cc -m32`, whose assembler compresses by default), so every link against them fails with "link merge failed: relocation error".  Basis: verified.  Task #52.
- [x] **LD-CUR-002** If merging an input relocation fails, then the linker shall report the input object, section, offset, relocation type by name, symbol and the underlying cause.
  Met: `merge_relocations` keeps the cause and adds the relocation; `elf_link_plan_link` hands the unfinished object back with the input's name instead of closing it; `ld` links through a plan with the inputs' own names and prints the lot.  Shown on a host build with an input the merge refuses: `ld: link merge failed: unsupported feature: section compressed by a method other than zlib / .debug_line_str / mz.o`, where it used to print `relocation error`.
  Evidence: `elf_link.c:885-886, 962-964, 967-968` turn every failure into `ELF_ERR_RELOC`; the output object is closed on error (`1143-1146`) so `ld.c:10629-10631` prints only the generic string.  Basis: traced.  Task #53.
- [x] **LD-CUR-003** When any input contains a GOT-class relocation, the linker shall create `.got`/`.got.plt` and define `_GLOBAL_OFFSET_TABLE_`, in static links as in dynamic ones.
  Met for i386: `plan_local_got_i386` makes a `.got` where there is none and something is GOT-relative, with a slot for each defined symbol reached through the table; `_GLOBAL_OFFSET_TABLE_` resolves to it.  Shown on the target: a program linked by a host build of this linker against today's `crt0.o`, `libc.a` and `libsys.a` runs, compiled both `-fno-pic` and `-fPIC`.  (x86-64 is LD-REL-002.)
  Evidence: `ld.c:9306-9315` resolves the symbol only if a GOT already exists; on i386 the special case is unreachable past `9210-9213`; `6158-6162` never treats `R_386_GOTPC` as needing one.  Today's `crt0.o` is PIC, so a static link fails on `_GLOBAL_OFFSET_TABLE_`.  On x86-64 `R_X86_64_GOTPC32` instead becomes an import with its own slot (`5914`, `9234`).  Basis: traced; reproduced on a host build.  Task #54.
- [x] **LD-CUR-004** The linker shall apply or remove the relocations of sections that are not loaded, and shall not copy relocation sections into an executable or shared object unless asked to.
  Met: `apply_all_relocations` relocates every section that has relocations, loaded or not, and drops them (`elf_section_clear_relocations`) once applied unless the output is relocatable.  Shown on a `-g` program: no `.rel.*` section in the executable where there were seven, 358404 bytes where there were 395728, and `addr2line` gives `m.c:5` for `main` where binutils could not read the DWARF at all.
  Evidence: `ld.c:10365` skips non-`ALLOC` sections; relocations stay attached and `elf_write.c:812-873` emits `.rel<section>` for each.  Basis: traced.  Task #56.
- [x] **AS-CUR-001** When the assembler (`usr.bin/as`) emits an in-place (REL) relocation against a local label, it shall store the label's offset within its section as the addend.
  Met: `local_label_addend()` supplies the offset to all three in-place writes.  Shown on the target: a program compiled by the old `cc`, assembled by the old `as` and linked by GNU `ld` prints its own strings, from code and from a pointer table in `.data` (addends 0, 5, 9 in `.rel.data`).
  Evidence: `as_elf_emit.c:14705-14709` writes the addend before `13004-13019` converts the label to section symbol plus offset, so the offset is lost; every `.L` reference on i386 resolves to the start of its section (a program printed the compiler's stamp string instead of its own).  Same ordering at `14485-14489`.  Basis: verified; reproduced.  Task #51.

## 2. Relocation processing

- [ ] **LD-REL-001** The linker shall compute each relocation by the formula its psABI gives, with the GOT base, the symbol's GOT slot, the TLS segment and the symbol's size available to the computation.
  Partly met: the i386 GOT-relative types (`GOTPC`, `GOTOFF`, `GOT32`, `GOT32X`) are computed in `apply_all_relocations` with the table's address and the symbol's slot, and a `GOT32X` to a defined symbol in a dynamic link is relaxed to `lea`.  Still open: TLS (LD-REL-005) and the size relocations.
  Evidence: `ld.c:10456` passes only type, place, symbol value and addend; `elf_reloc.c:159-167, 177-184` compute `R_386_GOT32`, `GOT32X` and `GOTOFF` as `S+A`; `SIZE32/SIZE64` likewise (`158, 329, 355`).  Basis: verified (`177`).  Tasks #55, #93.
- [ ] **LD-REL-002** When a GOT-relative relocation refers to a symbol defined in the output, the linker shall give the symbol a GOT entry or relax the instruction to a direct reference, for every instruction form the relocation may appear in.
  Evidence: `.got` is sized from imports only (`6218-6226, 6308-6316`); the one relaxation is x86-64 `mov`→`lea` keyed on a single opcode byte (`10464-10477`); `call *f@GOTPCREL(%rip)` and the i386 forms are left wrong.  Basis: traced.  Task #87.
- [x] **LD-REL-003** When an i386 PLT entry is built, the linker shall push the byte offset of its relocation in `.rel.plt`.
  Evidence: `ld.c:6982` pushes the entry index; `sbin/ld.so/ld_reloc.c:128` divides the argument by the relocation size, so lazy binding resolves the wrong import.  Basis: verified.  Task #84.
- [x] **LD-REL-004** When code refers to a function in a shared object through a PC-relative relocation, the linker shall route the reference through a PLT entry; when it refers to a data object that way, it shall not.
  Evidence: i386 `R_386_PC32` to an import gets no PLT (`6158-6162`, `9263`); x86-64 treats every `PC32` to an undefined `NOTYPE` symbol as a call (`6099-6102`, `9216-9226`), so data imports read PLT bytes.  Basis: traced.  Task #85.
- [ ] **LD-REL-005** Where the output uses thread-local storage, the linker shall compute TLS relocations relative to the thread pointer, allocate GOT slots and dynamic relocations for general- and initial-exec references to symbols of any binding, give TLS symbols segment-relative values, and emit a `PT_TLS` covering exactly `.tdata` and `.tbss` with their alignment.
  Partly met, for programs (`ET_EXEC` and PIE) and the variables they define: `.tbss` directly follows `.tdata` and takes no room in the image, `PT_TLS` covers the two with the alignment of the strictest, symbols have segment-relative values, and local-exec and initial-exec references on both architectures come to the constant distance from the thread pointer (`apply_tls_in_program`, initial-exec relaxed to local-exec).  Shown on the target, dynamically linked, on i386 and x86-64, with a second thread.  Still open: general- and local-dynamic references in a program, thread-local storage defined in a shared object, and a static i386 program (which the C library does not set up thread-local storage for with any linker).
  Evidence: `elf_reloc.c:185-197, 365-376` use `S+A` on absolute addresses; `STT_TLS` does not occur in `ld.c`; slots only for undefined symbols (`6118-6123, 6173-6178`); `R_X86_64_TLSLD` missing from the size table (`231-275`); `.dynsym` values absolute (`7979-7991`); `PT_TLS` spans everything between the two sections (`9542-9545, 10104-10113`) with alignment fixed at 8 (`10106`).  Basis: traced; absence verified.  Task #86.
- [x] **LD-REL-006** When a non-PIC executable refers to a data object in a shared object, the linker shall emit a copy relocation; where a text relocation is unavoidable, it shall set `DT_TEXTREL`; while `-z text` is in force it shall reject only relocations that would remain against read-only segments at run time.
  Evidence: no `R_*_COPY` or `DT_TEXTREL` anywhere; a dynamic `R_386_32` lands in `.text` (`6058-6060, 7061-7084`); `has_text_relocation` (`10158-10183`) rejects any executable section with any static relocation.  Basis: traced; absence verified.  Task #88.
- [x] **LD-REL-007** The linker shall accept i386 PC-relative results modulo 2^32, 64-bit absolute results over the whole address space on every host, and 8- and 16-bit absolute results in either signedness.
  Evidence: `elf_reloc.c:168-176` range-checks in wide arithmetic; `10-14, 331-334` fail above 2^63 where `__int128` is unavailable (the native i386 build); `198-204, 212-218` with `ld.c:9081-9100`.  Basis: traced.  Task #89.
- [x] **LD-REL-008** If an import has no entry in `.dynsym`, then the linker shall fail the link naming the symbol.
  Evidence: `ld.c:6636-6638, 6754-6761, 6960-6962, 7062-7064` skip it, leaving a zero PLT stub and GOT slot.  Basis: traced; trigger suspected (versioned imports).  Task #90.
- [ ] **LD-REL-009** When building an executable, the linker shall export the symbols its shared-object inputs refer to; when building a shared object, it shall use symbolic relocations for symbols that may be preempted.
  Evidence: `ld.c:5703-5709`; `5961-5972, 6758, 7066` use `RELATIVE` for every defined symbol.  Basis: traced.  Task #91.
  Partly met: an executable exports what its shared-object inputs refer to (`ctx->dso_wants`, filled by `note_dso_names`, asked by `dynsym_should_export`).  Still open: a shared object binds references to its own exported symbols at link time, as if `-Bsymbolic` were given, so a definition in the program does not preempt them.
- [ ] **LD-REL-010** The linker shall not change the size of a section after addresses are assigned, and shall record section indexes in `.dynsym` after the last reordering.
  Evidence: `ld.c:6566-6568, 6879-6881`; `7385, 7393, 7402`.  Basis: suspected.  Task #92.
- [ ] **LD-REL-011** Where an input defines an indirect function, the linker shall emit `IRELATIVE` relocations and PLT entries for it.
  Evidence: no `STT_GNU_IFUNC` or `IRELATIVE` handling in `ld.c`.  Basis: verified absent.  Task #93.
- [ ] **LD-REL-012** If planning or finalising dynamic data fails, then the linker shall name the symbol, section and relocation type concerned, and shall distinguish an unreadable or wrong-architecture shared object from an unsupported feature.
  Evidence: `ld.c:10751, 10803, 10811`; `10412-10421` print `<none>`; types printed as numbers; `8351-8353`.  Basis: traced.  Task #94.

## 3. Dynamic linking metadata

- [x] **LD-DYN-001** The linker shall record a shared object's `DT_SONAME` in `DT_NEEDED` and in version requirements, shall write `DT_SONAME` when `-soname`/`-h` is given, and shall write `DT_RPATH`/`DT_RUNPATH` when `-rpath` is given.
  Evidence: no soname handling in the file; the link-time basename is used (`7226-7237`, `5144-5145, 5278-5279, 5546-5547`); `-h` prints help (`10953`); `-soname`, `-rpath` and `--dynamic-linker` are unknown and their values become input files (`11569-11606`).  Basis: verified.  Task #83.
- [x] **LD-DYN-002** Where symbol versioning is used, the linker shall store unversioned names in `.dynsym`, emit a base version definition at index 1, and bind an unversioned reference only to a default or unversioned definition.
  Evidence: `ld.c:5478-5540` with `7352`; `4437, 5331-5341`; `4816, 5233-5243`; attribution to the first shared object (`5544-5548`) and skipping an earlier unversioned definition (`5212-5214`) are suspected.  Basis: traced.  Task #79.
- [x] **LD-DYN-003** While no `-m` mode was given, the linker shall fix the link mode from the first input and shall not change it when probing later shared objects.
  Evidence: `ld.c:4884, 4936, 5013, 5168` call `maybe_autoswitch_mode` with a count of 0.  Basis: traced.  Task #78.
- [x] **LD-DYN-004** The linker shall brand shared objects and position-independent executables with the Substrate OSABI as it does executables, in host and target builds alike.
  Evidence: `ld.c:10652-10660`.  Basis: suspected (run-time consequences not traced).  Task #102.

## 4. Output layout and segments

- [x] **LD-OUT-001** Where `-z relro` is in force, the linker shall place only read-only-after-relocation sections in `PT_GNU_RELRO` and shall end the segment on a page boundary.
  Evidence: `ld.c:9917-9935, 9437-9445, 10092-10102` (ordinary writable sections share the rank and fall inside the span); `10095` leaves `p_memsz` unpadded while `sbin/ld.so/ld_load.c:708-710` rounds the end down, so nothing is protected.  Basis: second part verified, first traced.  Task #95.
- [x] **LD-OUT-002** The linker shall generate `.eh_frame_hdr` and `PT_GNU_EH_FRAME` when the output contains `.eh_frame`.
  Met, when `--eh-frame-hdr` is given, which is when other linkers do it and what the substrate GCC always passes: `plan_eh_frame_hdr` and `fill_eh_frame_hdr`.  Shown on the target by a C++ program, linked through `g++ -B`, that catches what it throws.
  Evidence: `ld.c:10070` only passes through an input section of that name; `--eh-frame-hdr` is ignored (`11577`).  Basis: verified absent.  Task #99.
- [x] **LD-OUT-003** The linker shall not emit a loadable segment for sections that are all empty, and shall honour each section's alignment in its address.
  Evidence: `ld.c:10038-10047` with `elf_write.c:1412, 1458-1467`; `9453-9470` align the file offset.  Basis: traced.  Task #100.
- [x] **LD-OUT-007** Where no linker script places sections, the linker shall gather `.text.*`, `.rodata.*`, `.data.rel.ro.*`, `.data.*`, `.bss.*`, `.tdata.*`, `.tbss.*` and `.gcc_except_table.*` into the section named by the prefix, except in a relocatable output.
  Evidence: found after the audit, linking a C++ program through `g++ -B`: 97 output sections.  Basis: observed.  Task #120.
  Met: `default_output_name` through the section-name hook; elfobj no longer treats differing `SHF_MERGE`/entry size as an error when sections are gathered.  The same program has 41 sections and still catches its exception on the target.
- [x] **LD-OUT-004** If no entry symbol is defined, then the linker shall warn and shall not substitute an arbitrary symbol.
  Evidence: `ld.c:10217-10242, 10258`.  Basis: traced.  Task #101.
- [x] **LD-OUT-005** When writing its output, the linker shall write through an existing non-regular file or symbolic link rather than replace it, shall honour the umask, and shall remove the output if the link fails after it was written.
  Evidence: rename in `elf_util.c:366-388` (`ld -o /dev/null` as root replaces the device); `ld.c:10719, 10900` chmod to fixed modes; `10900-10907, 11661-11669`.  Basis: traced.  Task #96.
- [ ] **LD-OUT-006** Where `--gc-sections` is given, the linker shall treat as roots the entry, exported and `-u` symbols, init/fini code and arrays, exception tables and notes, and shall collect before sections are merged.
  Evidence: `ld.c:9580-9603, 9688-9700, 9745`; removed sections' symbols become undefined globals (`elf_sections.c:359-360`).  Basis: traced.  Task #97.
- [ ] **LD-OUT-007** When writing a map file or a reproduce bundle, the linker shall check every write, reject paths that do not fit, and quote values written into scripts.
  Evidence: `ld.c:8863-8868, 8888-8889, 8909-8959, 9045`.  Basis: traced.  Task #104.

## 5. Command line

- [x] **LD-OPT-001** If a `-z` keyword is not recognised, then the linker shall warn and continue; where `-z now` is given it shall set `DT_BIND_NOW`.
  Evidence: `ld.c:631-660` accepts six keywords; `11554-11562` exits 2.  Basis: verified.  Task #64.
- [x] **LD-OPT-002** The linker shall treat `-pie` as an executable link (entry required, undefined symbols diagnosed, branded, mode 0755) and shall not let `-static` change the output type.
  Evidence: `ld.c:11096-11101, 11103-11104`.  Basis: verified.  Task #98.
- [x] **LD-OPT-003** The linker shall recognise `-Ttext`, `-Tdata` and `-Tbss` as distinct from `-T`, joined `-oFILE`, and the options its specification lists (`-soname`, `-rpath`, `--emit-relocs`, `--strip-debug`, `-s`, `--build-id`), without depending on the order of prefix tests.
  Evidence: `ld.c:11514, 11032, 11565, 11569`; `10916`.  Basis: traced.  Task #103.

## 6. Inputs: archives and libraries

- [x] **LD-IN-001** If an archive member's recorded size or name length exceeds the file, then the linker shall reject the archive without reading or copying beyond it, on 32-bit hosts as on 64-bit.
  Evidence: `ld.c:3557-3564` copies a `#1/len` name before `3951` checks the member; `3951, 3960, 3965-3966, 4045` add a truncated size without overflow checks.  Basis: verified (first), traced.  Tasks #73, #74.
- [x] **LD-IN-002** The linker shall identify an archive member by a key that is unique for any path length.
  Evidence: `char member_key[96]` at `ld.c:3927, 3970`; with a path of 95 characters or more all members after the first are skipped.  Basis: verified.  Task #76.
- [x] **LD-IN-003** If registering an input's symbols fails, then the linker shall release the input exactly once.
  Evidence: `ld.c:3991-3992, 4130-4131` against `473`.  Basis: traced.  Task #75.
- [x] **LD-IN-004** If an archive is truncated or malformed, then the linker shall report it and fail; it shall recognise archives by content.
  Evidence: bare `break` and `return 0` at `ld.c:3931-3957, 4058`; suffix test at `4249`.  Basis: traced.  Task #81.
- [x] **LD-IN-005** When resolving `-lNAME`, the linker shall try the shared then the static form in each search directory in order, and shall pass over candidates of the wrong architecture.
  Evidence: `ld.c:4150-4159`; `4152, 4170, 4180, 4206`.  Basis: traced.  Task #82.

## 7. Linker scripts

- [x] **LD-SCR-001** If an expression is nested more deeply than a fixed limit, then the linker shall reject the script.
  Met: `lds_eval_unary` counts every level (parenthesis, unary operator, builtin argument) against `LD_MAX_SCRIPT_EXPR_DEPTH` (256).  Shown: 200000 nested `(`, `~` or `ALIGN(` give `s.lds:1:263: linker script parse error: expression nested too deeply`, where the linker died of SIGSEGV; 100 levels still evaluate.
  Evidence: unbounded recursion at `ld.c:1357-1459`.  Basis: traced.  Task #57.
- [x] **LD-SCR-002** The linker shall maintain a location counter and shall evaluate assignments, `PROVIDE` and `ASSERT` wherever the script grammar allows them, including inside `SECTIONS`.
  Met: the script is parsed into statements (`lds_script_parse`) and `script_assign_addresses` walks them with the counter; a statement between two input section descriptions is refused, the boundary not being kept.  Shown by `tests/usr.bin/ld/test_script.sh` and by programs linked through scripts that run on the target.
  Evidence: `ld.c:2608-2609` acts only at brace depth 0; top-level `. =` becomes a symbol named `.` (`2098`).  Basis: traced.  Task #58.
- [x] **LD-SCR-003** The script lexer shall treat `+`, `-` and `/` as operators.
  Met: the lexer reads a word as a name or as part of an expression according to what the parser expects there (`lds_lexer_t.names`).
  Evidence: `ld.c:1750, 1753-1754`.  Basis: verified.  Task #59.
- [x] **LD-SCR-004** If an expression names a symbol that is not defined, then the linker shall report an error.
  Evidence: `ld.c:1167-1168` returns 0.  Basis: verified.  Task #60.
- [x] **LD-SCR-005** When `PROVIDE` names a symbol an input defines, the linker shall leave the input's definition in place; `DEFINED` shall be true for such a symbol; `ALIGN(n)` shall align the location counter.
  Evidence: `ld.c:2095` with `9055-9064`; `1331`; `1282-1283`.  Basis: traced.  Tasks #61, #62, #63.
  Met: `PROVIDE` defines only what is referenced and undefined (`script_declare_in`); `DEFINED` asks `script_symbol_defined`, which knows the inputs' symbols; `ALIGN(n)` rounds the counter.  All three in `tests/usr.bin/ld/test_script.sh`.
- [x] **LD-SCR-006** The linker shall evaluate a script once against the final layout.
  Met: read once in `main`; its statements take effect in `script_assign_addresses`, after the layout, which they are part of.
  Evidence: applied twice (`ld.c:10661-10688`), parsed a third time (`11620`).  Basis: traced; layout consequence suspected.  Task #65.
- [x] **LD-SCR-007** The linker shall accept `PROVIDE_HIDDEN`, `HIDDEN`, compound assignment, `ASSERT` without a semicolon, `K`/`M` suffixes, and the functions `MAX`, `MIN`, `ABSOLUTE`, `CONSTANT`, `ORIGIN`, `LENGTH` and `?:`, or reject each with a diagnostic naming it.
  Evidence: `ld.c:2684-2687, 2766-2770, 2160-2164, 1844-1850, 1375-1379`.  Basis: traced.  Tasks #66, #67, #69.
  Met: all of them accepted and acted on, `?:` evaluating only the arm taken; also `ALIGNOF`, `SIZEOF_HEADERS`, the `DATA_SEGMENT_*` family and `SEGMENT_START`.  A function the linker does not have is an error naming it.  In `tests/usr.bin/ld/test_script.sh`.
- [x] **LD-SCR-008** When a script includes another, the linker shall continue in the including context, search the `-L` directories, and bound the total work.
  Evidence: `ld.c:2529-2555, 1951-1968`.  Basis: traced.  Task #68.
- [x] **LD-SCR-009** If the script cannot be tokenised, then the linker shall report the file and line.
  Evidence: silent `return -1` at `ld.c:2511-2516, 1737-1743`.  Basis: traced.  Task #70.
- [x] **LD-SCR-010** The linker shall match input-section patterns as shell globs.
  Met: `lds_glob`; and the patterns now decide which output section an input section is merged into (`script_output_name`, through elfobj's new section-name hook), which they did not before.
  Evidence: `ld.c:1970-1984, 2338, 2389`.  Basis: traced.  Task #71.
- [x] **LD-SCR-011** When a script orders sections, the linker shall keep the null section first.
  Met, and the suspicion was unfounded as stated: the object model has no null section (index 0 is the first real one).  What was wrong was that scripted sections were moved to the front of everything; they now change places only among themselves (`script_apply_sections`).
  Evidence: `ld.c:2822, 2063-2067`.  Basis: suspected.  Task #72.
- [x] **LD-SCR-012** Where a script has a `PHDRS` command, the linker shall terminate on malformed input, derive `PT_LOAD` flags from the sections placed in it, honour every `:phdr` on a section and inherit the previous one, emit each header once, and report a `PHDRS` it cannot parse.
  Evidence: `ld.c:2979-3062, 3006-3052` (hang, unbounded growth); `2871-2873, 2976`; `3089-3104`; `3154-3167`; `2967-2974` with `9986`.  Basis: traced.  Tasks #77, #80.

## 8. Structure

- [ ] **LD-STR-001** The linker shall resolve symbols through one table.
  Evidence: four implementations that have diverged — `symstate_t` (`ld.c:490-543, 3717, 3757`), `check_symbol_precedence` (`8615`), `merge_symbols` (`elf_link.c:722-847`), `check_undefined_symbols` (`10295`) — and seven linear name lookups (`260, 356, 490, 1064, 4418, 8578, 8718`).  Basis: traced.  Task #106.
- [ ] **LD-STR-002** The linker shall describe each target architecture in a table and share the code that uses it.
  Evidence: six i386/x86-64 pairs (`6000-6066`, `6068-6182`, `6204-6382`, `6494-7108`, `9214-9304`, `5906-5959`); 31 class tests in `patch_dynamic_tag_values` (`7797`).  Basis: measured.  Task #107.
- [x] **LD-STR-003** The linker's source shall be divided into translation units by function: utilities, diagnostics, script, plugin, input, shared objects, dynamic sections, layout, garbage collection, relocation, resolution, driver.
  Evidence: one file; the ranges for each unit are in the task.  Basis: measured.  Task #108.
- [ ] **LD-STR-004** The linker shall parse options from a table, keep options apart from link state, run its phases from an explicit list whose ordering constraints are checked, and release resources on one path per function.
  Evidence: `main` 753 lines and 55 returns (`10928-11680`); `ld_ctx_t` (`114-157`) with `const` cast away at `5116, 5168, 10319`; implicit ordering around `finalize_symbol_values_for_output` (`7961`, `10872`); 1,255 returns and drifted cleanup (`7230-7240, 6770-6772, 7076-7078`).  Basis: measured.  Task #109.
- [x] **LD-STR-005** The linker shall read a script once into a syntax tree and apply it in a separate pass.
  Evidence: three readers (`ld.c:2456, 2880, 8060`); `lds_ast_t` is written and never read.  Basis: traced.  Task #110.
- [ ] **LD-STR-006** The linker shall use one growable-array helper and shall not duplicate helpers the object library provides.
  Evidence: eleven hand-written vectors; duplicates listed in the task.  Basis: measured.  Task #111.
- [ ] **LD-STR-007** The linker's specification and architecture documents shall describe what the code does.
  Evidence: armap, plugin protocol, thunks, parallel parsing and several options are specified and absent; `lds_ast_t`, `self_path`, `warning_count` unused; one-bucket GNU hash (`5825`).  Basis: traced.  Task #105.

## 9. Performance

Timings are from a host build on synthetic inputs: 4,000 global symbols
0.56 s; 8,000 2.6 s; 20,000 14.6 s (31.1 s with `-Map`); a dynamic link with
100 / 400 / 1,600 imports from a 5,000-export shared object 0.89 / 1.99 /
7.87 s.

- [ ] **LD-PRF-001** The object library shall look symbols up by name in constant expected time, sort the symbol table in O(n log n), and grow section buffers geometrically.
  Evidence: `elf_symbols.c:99-113, 31-49`; `elf_link.c:804, 960`; insertion sort at `elf_symbols.c:283`; `elf_link.c:375, 430`.  About 70 % of the 20,000-symbol link.  Basis: measured.  Task #112.
- [ ] **LD-PRF-002** The linker shall open and parse each shared object once per link.
  Evidence: re-opened per undefined symbol at `ld.c:5010, 5165, 5116→4881`; about 90 % of a dynamic link.  Basis: measured.  Task #113.
- [ ] **LD-PRF-003** The linker shall use an archive's symbol index and parse only the members it loads.
  Evidence: `ld.c:3908-4051` re-parses every unloaded member on every pass; index skipped at `3946`; 97 % of a chained-archive link.  Basis: measured.  Task #114.
- [ ] **LD-PRF-004** The linker shall record each symbol's defining input when it is defined, reorder and remove sections in one pass, and reach output sections without a search by name per relocation.
  Evidence: `ld.c:9034→8791-8823`; `9550-9578`, `elf_sections.c:323-460`; `elf_link.c:762, 871, 905, 940`, `ld.c:9221-9309`; copies at `10383-10387, 10493`.  Basis: measured (`-Map`), traced.  Task #115.
