# ld.so Design Specification (Substrate)

## 1. Overview
`/libexec/ld.so` is the Substrate dynamic linker/loader for ELF ET_DYN shared
objects and dynamically-linked executables. It maps segments, resolves
dependencies, applies relocations, sets up TLS, runs initializers, and transfers
control to the program entry point. The loader follows POSIX and System V ELF ABI
requirements while aligning runtime behavior with BSD-style loaders (search
paths, diagnostics, secure-exec handling).

## 2. Goals
- **POSIX/ELF-ABI compliance:** Correct interpretation of ELF headers, program
  headers, dynamic tables, relocation records, and symbol/versioning sections.
- **BSD compatibility:** Search path semantics, error reporting style, and
  security rules comparable to BSD `ld.so` implementations.
- **Performance:** Favor GNU hash lookups when available, minimize relocation
  passes, and keep dependency walks deterministic.
- **Predictability:** Deterministic search order and reproducible symbol
  resolution across runs.
- **Integration:** Compatible with Substrate kernel TLS (i386 GS segment) and
  Substrate userland ABI conventions.

## 3. Non-Goals
- A full `ldconfig`-style cache in the first iteration.
- Support for non-ELF object formats or foreign ABI object files.
- Runtime auditing APIs (e.g., `LD_AUDIT`).
- Full `dlopen(3)`/`dlsym(3)` semantics in the initial bootstrap phase (later
  userland work may extend to RTLD APIs).

## 4. ABI Targets
- **i386 (primary):** System V i386 ABI. Relocations are REL (implicit addend).
- **x86_64:** System V AMD64 ABI. Relocations are RELA (explicit addend).
  Built from the same sources as `/sbin/ld64.so`; see section 22.

## 5. Input/Output Contracts
### 5.1 Inputs
- **Main executable** mapped by the kernel, with `AT_*` auxiliary vector.
- **PT_INTERP** path specifying `/libexec/ld.so` (Substrate dynamic linker).
- **Environment** and **auxv** data provided by the kernel.
- **Filesystem** access to locate and map shared objects.

### 5.2 Outputs
- **Mapped shared objects** in process address space.
- **Resolved relocations** and **initialized GOT/PLT**.
- **TLS blocks** allocated and linked to the thread pointer.
- **Execution transfer** to the executable entry point.

## 6. Kernel/Loader Interface
### 6.1 Auxiliary Vector Contract
The kernel provides these entries (minimum for correct operation):
- `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`: Program header table details.
- `AT_ENTRY`: Executable entry point (transfer target).
- `AT_BASE`: Base address where `ld.so` is loaded.
- `AT_PAGESZ`: Page size used for alignment/mapping.
- `AT_EXECFN`: Executable path (for diagnostics).

The loader treats missing required entries as fatal.

### 6.2 PT_INTERP Handling
- The kernel loads the interpreter specified by PT_INTERP.
- `ld.so` validates that PT_INTERP matches `/libexec/ld.so` (or a compatible
  alias), otherwise errors with a clear diagnostic.

### 6.3 ABI-Required Personality
- The loader assumes native Substrate ABI; Linux/FreeBSD personality handling
  remains in the kernel or userland wrappers.

## 7. ELF Feature Support
### 7.1 Mandatory Dynamic Tags
`ld.so` must parse and track (when present):
- `DT_STRTAB`, `DT_SYMTAB`, `DT_STRSZ`, `DT_SYMENT`
- `DT_HASH` and/or `DT_GNU_HASH`
- `DT_NEEDED`
- `DT_REL`, `DT_RELSZ`, `DT_RELENT`
- `DT_JMPREL`, `DT_PLTRELSZ`, `DT_PLTREL`
- `DT_INIT`, `DT_FINI`, `DT_INIT_ARRAY`, `DT_FINI_ARRAY`
- `DT_RPATH`, `DT_RUNPATH`
- `DT_SONAME`
- `DT_VERSYM`, `DT_VERNEED`, `DT_VERDEF`
- TLS: PT_TLS metadata and associated dynamic tags

### 7.2 Optional Tags (Best-Effort)
- `DT_TEXTREL`: Allowed for compatibility but must emit warning; future hard
  error once text-relocs are deprecated.
- `DT_FLAGS`, `DT_FLAGS_1`: Handle `DF_BIND_NOW`, `DF_1_NOW`, and `DF_1_PIE`.
- `DT_DEBUG`: Export debug structure pointer for debuggers if present.

### 7.3 Relocations
- **REL (i386):** `DT_REL` / `DT_RELSZ` / `DT_RELENT`.
- **RELA (x86_64):** `DT_RELA` / `DT_RELASZ` / `DT_RELAENT`.

### 7.4 PLT/GOT
- Handle `DT_JMPREL` relocations for `R_*_JMP_SLOT` and
  `R_*_GLOB_DAT`.
- Support lazy binding unless `LD_BIND_NOW`, `DF_BIND_NOW`, or `DF_1_NOW` disable
  it.

### 7.5 TLS Models
- **Local Exec (LE)**
- **Initial Exec (IE)**
- **Global Dynamic (GD)**
- **Local Dynamic (LD)**

TLS must integrate with Substrate kernel TLS (the `%gs` base on i386, the
`%fs` base on x86_64); per-thread TLS must be allocated and the thread
pointer adjusted per ABI.

## 8. Loader Data Model
### 8.1 Object Descriptor
Each loaded object has a descriptor containing:
- File path, inode/device identifiers for de-dup.
- Load base, mapping range, and segment permissions.
- Pointers to `DT_*` tables (symtab, strtab, hash).
- Relocation table pointers and sizes.
- TLS metadata (alignment, size, module ID).
- Dependency edges (DT_NEEDED list).
- Flags (PIE, RTLD_LOCAL/RTLD_GLOBAL once RTLD APIs are added).

### 8.2 Global Loader State
Global state tracks:
- Loaded object list (load order).
- Symbol resolution scopes.
- TLS module registry.
- Loader flags (`secure`, `bind_now`, `trace_loaded`).
- Debug hooks (`DT_DEBUG`).

## 9. Loader Phases (Detailed)
1. **Bootstrap:**
   - Relocate `ld.so` itself using static relocation table.
   - Initialize minimal runtime (basic allocators, logging).
2. **Primary ELF mapping:**
   - Map executable segments and collect PT_DYNAMIC info.
   - Record program headers for auxv/`AT_PHDR` validation.
3. **Dependency discovery:**
   - Parse `DT_NEEDED` and construct the dependency graph.
4. **Search & mapping:**
   - Resolve each needed shared object using search rules.
   - Map all segments with correct permissions and alignment.
5. **Dynamic parsing:**
   - Build symbol tables, hash tables, version maps, and TLS metadata.
6. **Relocations:**
   - Apply REL/RELA (non-PLT) relocations first.
   - Apply PLT relocations (eager or lazy depending on policy).
7. **TLS setup:**
   - Assign module IDs.
   - Allocate per-thread TLS blocks and apply TLS relocations.
8. **Initialization:**
   - Execute `DT_INIT` and `DT_INIT_ARRAY` in dependency order.
9. **Transfer:**
   - Jump to the main executable entry point (from `AT_ENTRY`).

## 10. Memory Mapping Rules
- Respect `p_align` for segment mapping.
- Map with `PROT_READ`, `PROT_WRITE`, `PROT_EXEC` derived from `p_flags`.
- Use `MAP_PRIVATE` for text/data; use `MAP_ANON` for BSS pages beyond file size.
- Guard against overflow in offset/size calculations.
- Enforce W^X when possible (no simultaneous PROT_WRITE and PROT_EXEC on text
  segments after relocations complete).
- Text segments should be set read/exec after relocation when possible.

## 11. Dependency Search Policy
### 11.1 Search Order
When resolving `DT_NEEDED` or explicit loads:
1. `LD_PRELOAD` objects (non-secure only).
2. Main executable.
3. Breadth-first traversal of `DT_NEEDED` in order.
4. `DT_RPATH` / `DT_RUNPATH` paths on the referencing object.
5. Default system paths: `/lib`, `/usr/lib`, `/usr/local/lib` (the 64-bit
   linker: `/lib64`, `/usr/lib64`, `/usr/local/lib64`).

### 11.2 `DT_RPATH` vs `DT_RUNPATH`
- If **`DT_RUNPATH`** is present, use it for direct dependency search and
  **ignore** `DT_RPATH`.
- If only **`DT_RPATH`** is present, apply it to the full dependency chain
  (legacy behavior).

### 11.3 Path Expansion
- `$ORIGIN` expands to the directory containing the referencing object.
- `$LIB` resolves to architecture-specific library directory if applicable
  (currently `/lib` for i386).
- `$PLATFORM` resolves to the ABI/platform triplet (future expansion).

### 11.4 Canonicalization
- Normalize path separators and collapse `.`/`..` when safe.
- Reject empty path elements for secure binaries.
- Avoid resolving symlinks unless necessary for de-duplication.

## 12. Symbol Resolution Specification
### 12.1 Resolution Order
For each undefined reference:
1. **`LD_PRELOAD` objects** (left-to-right),
2. **Main executable**,
3. **Direct dependencies** and their dependency trees in BFS order,
4. **`DT_RPATH`/`DT_RUNPATH`** for the referencing object,
5. **Default system paths**.

### 12.2 Scope and Visibility
- Respect ELF visibility attributes (`STV_DEFAULT`, `STV_HIDDEN`, `STV_PROTECTED`).
- Hidden symbols are not eligible for interposition.
- Protected symbols resolve locally for the defining object.

### 12.3 Versioning Policy
- Honor `DT_VERSYM`, `DT_VERNEED`, `DT_VERDEF`.
- A versioned request must match the required version index.
- Default to base version (index 1) when none is specified.

### 12.4 SONAME Handling
- Use `DT_SONAME` to identify and de-duplicate shared objects.
- If absent, fall back to the path basename.

### 12.5 Hash Table Strategy
- Prefer **`DT_GNU_HASH`** when present for lookup performance.
- Fall back to **`DT_HASH`** if GNU hash is absent.
- If both are present, use GNU hash and validate with SysV hash during
  development builds as needed.

## 13. Relocation Strategy
- Apply REL/RELA in object load order.
- `R_*_RELATIVE` handled first for speed (no symbol lookup).
- `R_*_COPY` handled last, after all objects are fully relocated.
- PLT relocations resolved lazily unless `LD_BIND_NOW` or `DF_BIND_NOW` is set.
- Relocation overflow or unsupported relocation type is fatal.

## 14. Lazy Binding (PLT Resolver)
### 14.1 Resolver Flow
- The first call to a PLT entry traps to the resolver stub.
- The resolver computes the relocation index and resolves the target symbol.
- The GOT/PLT entry is patched with the resolved function address.
- Subsequent calls jump directly to the resolved target.

### 14.2 Disabling Lazy Binding
- If `LD_BIND_NOW` or `DF_BIND_NOW` is set, all PLT entries are resolved at
  load time.

### 14.3 As Implemented
Both linkers bind lazily; the code is shared (`ld_reloc.c`) apart from
the trampoline.

- `ld_reloc_lazy_setup()` adds the load bias to each `JMP_SLOT` word
  (which then points at its stub in `.plt`), and stores the object's
  descriptor in `GOT[1]` and `ld_plt_trampoline` in `GOT[2]`.
- The trampoline (`ld_plt_i386.S`, `ld_plt_amd64.S`) preserves the
  registers a call may carry arguments in, calls `ld_plt_fixup()`, and
  jumps to what it returns.  i386 saves `%eax`, `%edx` and `%ecx`
  (regparm and fastcall; `___tls_get_addr` takes its argument in `%eax`);
  amd64 saves the six integer argument registers, `%rax`, `%r10` and the
  FXSAVE state.  The PLT stub names its `DT_JMPREL` entry by byte offset
  on i386 and by index on amd64.
- `ld_plt_fixup()` resolves the symbol under the `dlopen` lock, calls an
  indirect function's resolver if that is what it found, and stores the
  result in the slot.  A symbol nothing defines is fatal at that point
  (`lazy binding failed`, status 127) instead of at load time.
- Binding is eager when the object carries `DT_BIND_NOW`, `DF_BIND_NOW`
  or `DF_1_NOW` (every system library does: `SHLIB_LDFLAGS` has
  `-z now`), when `LD_BIND_NOW` is set to a non-empty value, for the
  objects a `dlopen(RTLD_NOW)` loads, and for any `DT_JMPREL` that holds
  something other than `JMP_SLOT`/`IRELATIVE` entries or a slot that is
  still zero.
- Limits.  `RTLD_NOW` does not go back and bind an object that an
  earlier call left lazy.  A resolver run from the fixup must not
  disturb vector argument registers the trampoline does not save: all of
  them on i386, the upper halves of `%ymm` on amd64.  A process that
  forks while another thread is inside `dlopen` gets a child in which
  the first unbound call blocks on the `dlopen` lock.

### 14.4 Indirect Functions
An `IRELATIVE` relocation, and a `GLOB_DAT`, `JMP_SLOT` or absolute
relocation whose symbol resolves to an `STT_GNU_IFUNC` definition, store
what a resolver function returns.  The resolver is code in some loaded
object, so it must not run before that object is relocated: the main
pass skips such an entry and marks the object (`has_ifunc`), and a third
pass, `ld_relocate_ifunc()`, applies them after the `COPY` pass, at
startup and in `dlopen`.  In that pass `ld_reloc_apply()` touches
nothing else, which on i386 -- where the addend is the relocated word
itself -- is what keeps the other entries from being applied twice.
`dlsym` and the other lookups made after relocation call the resolver
directly.

## 15. TLS Details
### 15.1 TLS Layout
- Each module with PT_TLS contributes a TLS block.
- The loader assigns a module ID and computes offsets respecting alignment.
- For i386, the thread pointer points to the TLS base as defined by ABI and
  Substrate kernel TLS setup.

### 15.2 `__tls_get_addr`
- Provide a resolver for GD/LD models.
- Cache resolved addresses per thread to reduce overhead.

## 16. Environment Variables
`ld.so` recognizes the following variables for **non-secure** binaries:
- **`LD_LIBRARY_PATH`**: extra search paths (colon-separated).
- **`LD_PRELOAD`**: preload DSOs (colon-separated).
- **`LD_DEBUG`**: diagnostics (`libs`, `reloc`, `symbols`, `all`).
- **`LD_BIND_NOW`**: disable lazy binding, resolve all PLT now.
- **`LD_TRACE_LOADED_OBJECTS`**: print dependencies and exit.

## 17. Secure Execution (setuid/setgid)
If the executable is setuid or setgid, `ld.so` enters secure mode:
- Ignore `LD_LIBRARY_PATH`, `LD_PRELOAD`, and `LD_DEBUG`.
- Only search trusted system directories (`/lib`, `/usr/lib`, `/usr/local/lib`).
- Require absolute paths for `DT_NEEDED` entries (if present).
- Disallow audit/profiling extensions (not supported in Substrate).
- Refuse to load DSOs from writable directories when running setuid/setgid.

## 18. Diagnostics & Debugging
- `LD_DEBUG=libs`: report search paths and object load order.
- `LD_DEBUG=reloc`: report relocation application summary.
- `LD_DEBUG=symbols`: report symbol resolution decisions.
- Emit warnings for text relocations and invalid RPATH tokens.
- `LD_TRACE_LOADED_OBJECTS` prints dependency tree and exits with status 0.

## 19. Error Handling
- Reject malformed ELF headers, invalid program headers, and overflowed sizes.
- Emit explicit errors for missing symbols and unsupported relocations.
- `LD_DEBUG` controls logging without changing exit status.
- On fatal errors, report object path and symbol name before aborting.

## 20. Testing Expectations (Design-Level)
- **ELF validation:** malformed headers, invalid sizes, broken PT_DYNAMIC.
- **Search path:** RPATH/RUNPATH precedence, `$ORIGIN` expansion.
- **Relocations:** REL, PLT, TLS, and copy relocation behavior.
- **Secure exec:** ignoring LD_* variables for setuid/setgid.
- **Symbol resolution:** versioned symbol failure cases and interposition.
- **Lazy binding:** PLT resolver patching and bind-now path.

## 21. Acceptance Criteria
- Design document reviewed and approved.
- Relocation matrix enumerates all targeted relocations for i386 and x86_64.
- Symbol resolution rules documented with path and precedence details.
- Secure-exec behavior and environment variable rules documented.

## 22. The Two Builds: `ld.so` and `ld64.so`

One source tree, `sbin/ld.so/`, builds both dynamic linkers:

| Command | Output | Installed as | Loads |
| :------ | :----- | :----------- | :---- |
| `make -C sbin/ld.so` | `ld.so` | `/sbin/ld.so` | `ELFCLASS32` / `EM_386` |
| `make -C sbin/ld.so ARCH=x86_64` | `obj-x86_64/ld64.so` | `/sbin/ld64.so` | `ELFCLASS64` / `EM_X86_64` |

The 64-bit objects go under `obj-x86_64/` (the `$(O)` convention of
`Makefile.inc`), so the two builds never share a file.  The kernel maps
whichever one the executable's `PT_INTERP` names; a 64-bit program runs
on the x86_64 kernel only.

### 22.1 What is shared

Loading, the object list, dependency search, symbol resolution
(`DT_GNU_HASH`, `DT_HASH`, GNU versioning), the relocation passes and
their ordering, TLS layout, constructors/destructors and the `dl*`
interface are one implementation.  It is written against word-size
neutral names declared in `ld.h`:

* `ld_addr` -- an address, load bias or ELF size of the native class
  (`unsigned int` on i386, exactly the type the 32-bit linker always
  used; `unsigned long` on amd64).
* `Elf_Ehdr`, `Elf_Phdr`, `Elf_Dyn`, `Elf_Sym` -- the ELF32 or ELF64
  structure.  The versioning structures (`Elf_Verdef`, ...) are the same
  in both classes.
* `Elf_Reloc`, `LD_DT_REL`, `LD_DT_RELSZ` -- the relocation entry and the
  dynamic tags that locate the table: `Elf_Rel` / `DT_REL` on i386,
  `Elf_Rela` / `DT_RELA` on amd64.  `DT_JMPREL` holds the same entry type.
* `LD_BLOOM_BITS` -- the width of a `DT_GNU_HASH` bloom word (the native
  word; the rest of that table, and all of `DT_HASH`, is 32-bit words in
  both classes).

### 22.2 What is per architecture

| Piece | i386 | amd64 |
| :---- | :--- | :---- |
| Start code | `ld_start.S`: self-relocates `R_386_RELATIVE` from `DT_REL`, passes `%esp` to `ld_main`, jumps to the entry with `%esp` restored and `%edx` = 0 | `ld_start_amd64.S`: self-relocates `R_X86_64_RELATIVE` from `DT_RELA`, passes `%rsp`, jumps to the entry with `%rsp` restored, `%rdi` = that stack pointer and `%rdx` = 0 |
| Relocation types | `ld_reloc_i386.c` | `ld_reloc_amd64.c` |
| Raw system calls (`ld_io.c`) | `int $0x80`, arguments on the stack, `-errno` in `%eax` | `syscall`, arguments in `%rdi %rsi %rdx %r10 %r8 %r9`, error in the carry flag (negated to `-errno` for the callers) |
| `lseek` | three arguments (see note) | `fd, off_lo, off_hi, whence` |
| `getdents` record (ld.so.conf globbing) | `d_reclen` at 8, name at 10 | `d_reclen` at 16, name at 24 |
| Thread pointer | `%gs` base | `%fs` base |
| TCB | 8 bytes: self, DTV | 64 bytes: self, DTV, spare (keeps `%fs:0x28` clear of the DTV) |
| `tls_index` | two 4-byte words; `___tls_get_addr` (regparm) and `__tls_get_addr` | two 8-byte words; `__tls_get_addr` only |
| Built-in search directories | `/lib`, `/usr/lib`, `/usr/local/lib` | `/lib64`, `/usr/lib64`, `/usr/local/lib64` |
| Own name (`LD_TRACE_LOADED_OBJECTS`, fatal errors) | `ld.so` | `ld64.so` |

Note on `lseek`: the 32-bit linker has always issued `SYS_lseek` with
three arguments although the kernel's takes the offset as two halves; the
kernel tolerates the resulting out-of-range `whence` as "no change", and
the only seek the linker makes is to the position it is already at
(`e_phoff`, straight after the file header).  That behaviour is left
alone; the 64-bit linker passes the four arguments.

Both builds set the thread pointer with the native `SYS_SET_GSBASE`
call, which sets the `%gs` base of a 32-bit process and the `%fs` base
of a 64-bit one.  The DTV pointer is the second TCB word in both, which
is where libc's `__tls_get_addr` reads it (`lib/c/src/tls.c`).

### 22.3 amd64 relocations

`R_X86_64_NONE`, `64`, `PC32`, `COPY`, `GLOB_DAT`, `JUMP_SLOT`,
`RELATIVE`, `TPOFF64`, `DTPMOD64`, `DTPOFF64` and `IRELATIVE`.  The
addend is `r_addend`; the relocated word is overwritten and never read,
so applying a `RELATIVE` twice would be harmless there (the per-object
`relocated` guard still applies to both).  A `PC32` whose target is out
of 32-bit range is refused.  Lazy binding and indirect functions are as
described in sections 14.3 and 14.4.

### 22.4 Sharing a root

The two architectures' libraries are installed side by side (`/lib` and
`/lib64`), and both linkers read `/etc/ld.so.conf`.  A directory named
there may therefore hold libraries of the other word size.  Each linker
checks `EI_CLASS` and `e_machine` before mapping anything and treats a
mismatch like a missing file: the search moves on to the next directory,
and a `dlopen` of such a path fails with an error.  There is no separate
64-bit configuration file.

### 22.5 Building programs against it

`Makefile.bin.inc` honours `DYNAMIC = 1` for `ARCH=x86_64`: the program
is linked as a PIE with `--dynamic-linker=/sbin/ld64.so` against
`lib/*/obj-x86_64/lib*.so.0`, and each `-L$(TOP)/...` in its `LDADD` is
pointed at that directory's `obj-x86_64/`.  Programs without
`DYNAMIC = 1`, or built with `DYNAMIC=0` on the command line, are linked
statically as before.  The 64-bit `libm.so.0` takes the compiler's
complex-arithmetic helpers from `libgcc.a` rather than `libgcc_s.so.1`,
since no 64-bit `libgcc_s` is installed.

### 22.6 Tests

`tests/sbin/ld64/` holds target tests for the 64-bit linker (printf and
argv/envp, executable TLS and errno, libm, libpthread threads with
per-thread TLS, `dlopen` of a module with constructors and TLS, rejection
of a 32-bit object) and `run-ld64-tests.sh`, which also runs the in-tree
shell built both dynamically and statically.  Three of its tests cover
what both linkers do -- `ifunc`, `lazy` and `rtldnext` -- and
`make -C tests/sbin/ld64 ARCH=i386` builds those as non-PIE 32-bit
programs for `/sbin/ld.so`; `run-ld64-tests.sh DIR 32` runs them.
