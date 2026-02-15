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
- **x86_64 (planned):** System V AMD64 ABI. Relocations are RELA (explicit addend).

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
- **RELA (x86_64 planned):** `DT_RELA` / `DT_RELASZ` / `DT_RELAENT`.

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

TLS must integrate with Substrate kernel TLS (i386 GS segment); per-thread
TLS must be allocated and the thread pointer adjusted per ABI.

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
5. Default system paths: `/lib`, `/usr/lib`, `/usr/local/lib`.

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
