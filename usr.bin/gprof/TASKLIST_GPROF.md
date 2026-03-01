# `usr.bin/gprof` Tasklist

Goal: implement `gprof`‑compatible analysis for `gmon.out` data + ELF symbols.

---

## 1. `libelfobj` Integration

- [ ] Open executable/shared‑object via `elf_open()`.
- [ ] Read `.symtab` (preferred) or `.dynsym` for function symbol resolution.
- [ ] Use `elf_symbol_count()` / `elf_symbol_get()` to build symbol address table.
- [ ] Filter to `STT_FUNC` symbols; include `STB_GLOBAL`, `STB_LOCAL`, `STB_WEAK`.
- [ ] Sort symbols by `st_value` for address‑to‑function lookup via binary search.
- [ ] Read `.text` section bounds (`elf_section_addr()` / `elf_section_size()`) for histogram validation.
- [ ] `elf_close()` after symbol table is loaded.

## 2. `gmon.out` Parsing

### 2a. File Format
- [ ] Parse `gmon_hdr` (magic `0x676d6f6e`, version 1, spare fields).
- [ ] Validate header magic and version; reject unknown versions with diagnostic.
- [ ] Parse record types in a loop until EOF:
  - [ ] `GMON_TAG_TIME_HIST` (0): histogram record.
  - [ ] `GMON_TAG_CG_ARC` (1): call‑graph arc record.
  - [ ] `GMON_TAG_BB_COUNT` (2): basic‑block count record (optional support).
- [ ] Handle big/little‑endian `gmon.out` based on ELF endianness.

### 2b. Histogram Record
- [ ] Parse: low‑pc, high‑pc, number of bins, profiling rate, dimension string.
- [ ] Read `hist_size` bins of `uint16_t` sample counts.
- [ ] Validate: `low_pc` and `high_pc` fall within `.text` bounds.
- [ ] Map each bin to a virtual address range: `addr = low_pc + bin_index * bin_size`.
- [ ] Distribute bin counts to functions by matching address ranges against sorted symbol table.

### 2c. Call‑Graph Arc Record
- [ ] Parse: `from_pc`, `self_pc`, `count`.
- [ ] Resolve `from_pc` → caller function name.
- [ ] Resolve `self_pc` → callee function name.
- [ ] Aggregate: accumulate arc counts for each unique (caller, callee) pair.

### 2d. Basic‑Block Count Record (optional)
- [ ] Parse: number of BBs, array of (address, count) pairs.
- [ ] Map to functions by address.

## 3. Flat Profile Report

- [ ] Column headers: `%time`, `cumulative seconds`, `self seconds`, `calls`, `self ms/call`, `total ms/call`, `name`.
- [ ] Sort by self‑time descending (default).
- [ ] Calculate time from histogram: `self_seconds = (bin_count / profiling_rate)`.
- [ ] Calculate `cumulative seconds` as running sum.
- [ ] `calls` from call‑graph arcs (callee count for each function).
- [ ] `self ms/call` = `self_seconds / calls * 1000`.
- [ ] `total ms/call` = `(self_seconds + children_time) / calls * 1000` (requires propagation).
- [ ] Functions with zero samples and zero calls: omit from flat profile.

## 4. Call Graph Report

### 4a. Graph Construction
- [ ] Build directed graph: node = function, edge = arc with count.
- [ ] Detect cycles (recursive functions / mutual recursion).
- [ ] Handle cycles by grouping into strongly‑connected components (SCCs).

### 4b. Time Propagation
- [ ] Propagate children time from callees to callers.
- [ ] Distribute time proportionally when a function is called by multiple parents.
- [ ] For SCCs: distribute time evenly within the cycle, then propagate out.

### 4c. Output Format
- [ ] Print index number, `%time`, `self`, `children`, `called`, `name` for each function.
- [ ] Below each function: list callers (indented, with arc counts).
- [ ] Above each function: list callees (indented, with arc counts).
- [ ] Separator lines between function groups.
- [ ] Print `[<index>]` cross‑references.

### 4d. Sorting
- [ ] Default: sort by decreasing `self + children` time.
- [ ] Secondary sort: by name for ties.

## 5. Annotated Source (optional)

- [ ] `-A`: per‑line execution counts overlaid on source.
- [ ] Requires DWARF line info (defer unless `libelfobj` DWARF support available).
- [ ] Map histogram bins / BB counts to source lines.

## 6. Flags and Options

- [ ] `-b`: suppress verbose blurbs / explanatory text.
- [ ] `-p`: flat profile only (no call graph).
- [ ] `-q`: call graph only (no flat profile).
- [ ] `-z`: include zero‑count functions in flat profile.
- [ ] `-c`: discover children of static functions using arc data.
- [ ] `-e <function>`: exclude function from reports.
- [ ] `-E <function>`: exclude function and its descendants.
- [ ] `-f <function>`: focus on function only.
- [ ] `-F <function>`: focus on function and its descendants.
- [ ] `-s`: accumulate multiple `gmon.out` files into `gmon.sum`.
- [ ] `-k <from> <to>`: delete arcs from `<from>` to `<to>`.
- [ ] Default input file: `gmon.out`; accept alternate filename as positional arg.
- [ ] Default executable: `a.out`; accept alternate as second positional arg.

## 7. Missing / Partial Symbol Handling

- [ ] Address not matching any symbol: report as `<unknown>` or `0x<addr>`.
- [ ] Stripped executable (no `.symtab`): try `.dynsym`; warn if no symbols at all.
- [ ] Symbol table with gaps (non‑contiguous functions): attribute samples in gaps to nearest preceding symbol.
- [ ] Multiple symbols at same address: use the first `STB_GLOBAL` or `STT_FUNC`.

## 8. Shared Object / Archive Support

- [ ] If executable is dynamically linked: note that shared library code won't have histogram samples (profiling counts only in text segment).
- [ ] Support profiling data from `LD_PROFILE` for shared objects (deferred).
- [ ] Archive input on command line: not applicable for `gprof` (executables only).

## 9. Error Handling

- [ ] No `gmon.out`: clear error message.
- [ ] `gmon.out` from a different executable (histogram range mismatch): warn and proceed best‑effort.
- [ ] Non‑ELF executable: error, exit 1.
- [ ] `gmon.out` format errors: warn, report partial data.

## 10. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` support for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/gprof`.

## 11. Testing

### 11a. Flat Profile Tests
- [ ] Known `gmon.out` from a trivial program: verify function names, non‑zero times for hot function.
- [ ] `-z` flag: zero‑count functions appear.
- [ ] `-e func`: `func` excluded from output.
- [ ] Sorted by self‑time descending.

### 11b. Call Graph Tests
- [ ] Simple call chain `main → a → b`: verify arcs and index cross‑refs.
- [ ] Recursive function: verify cycle detection and annotation.
- [ ] Function called by multiple parents: verify proportional time distribution.

### 11c. Symbol Resolution Tests
- [ ] Unstripped executable: all functions named.
- [ ] Stripped executable: functions appear as `<unknown>` or hex addresses.
- [ ] Executable with gaps in symbol coverage: samples attributed to nearest function.

### 11d. Data Accumulation Tests
- [ ] `-s`: two `gmon.out` files summed into `gmon.sum`, then analyzed.

### 11e. Format Tests
- [ ] `-b`: no blurb text.
- [ ] `-p`: only flat profile, no call graph.
- [ ] `-q`: only call graph, no flat profile.
- [ ] Deterministic: same input always produces identical output.

### 11f. Edge Cases
- [ ] `gmon.out` with zero histogram samples: empty flat profile, arcs still shown.
- [ ] `gmon.out` with no arcs: flat profile only from histogram.
- [ ] Very large `gmon.out` (millions of arcs): completes in reasonable time.

## 12. Man Page

- [ ] Write `gprof.1` covering all flags, input files, output formats.
- [ ] Document `gmon.out` format briefly.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
