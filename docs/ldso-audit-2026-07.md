# /sbin/ld.so audit — 2026-07

Full read of `sbin/ld.so/` (3,303 lines: ld.h, ld_start.S, ld_io.c,
ld_load.c, ld_main.c, ld_resolve.c, ld_reloc.c, ld_tls.c, ld_dl.c,
Makefile) at commit f815b4ee0.  Findings numbered LDSO-NN, ranked by
severity.  Status: **all open** (audit only — no fixes applied).

What's solid: the `ld_mmap_failed()` -errno-range check (high-address
mappings handled), the two-pass R_386_COPY design, the canonical-PLT
function-address-equality rule, version-aware hash-chain walking (multi
version chains handled), AT_SECURE gating of LD_PRELOAD, per-object
non-idempotent-reloc guards, and the conf-include glob parser matching
the kernel's real getdents layout.

## High — latent corruption

### LDSO-01: dlopen of a PT_TLS-carrying DSO silently corrupts the TCB/DTV
`ld_setup_tls()` runs exactly once, from `ld_main()` (ld_main.c:527).
A DSO loaded later via `__ldso_dlopen()` never gets `tls_offset` /
`tls_modid` assigned (both stay 0 — ld_load.c:344).  Its relocation
pass then:
- **R_386_TLS_TPOFF** (ld_reloc.c:196): `*p = st_value - 0 + A` — a
  *positive* %gs offset.  Variant-II TLS lives at negative offsets;
  positive offsets read/write the TCB self-pointer (gs:0), the DTV
  pointer (gs:4), and the DTV itself.  Silent memory corruption.
- **R_386_TLS_DTPMOD32** (ld_reloc.c:211): `*p = obj->tls_modid` = 0;
  `__tls_get_addr` returns NULL for `ti_module == 0` (ld_tls.c:204) →
  NULL deref at first use.

Failure scenario: any plugin/module with `__thread` or C++
`thread_local` data loaded by dlopen (X11 modules, TDE KLibLoader
plugins).  Minimum fix: make `ld_relocate` **fail** on TLS relocs when
`tls_modid == 0 && tls_memsz != 0`, so dlopen errors cleanly instead of
corrupting.  Real fix: assign modid/offset at load time and extend the
static block reservation (glibc's surplus-TLS approach).

### LDSO-02: TLS blocks can be misaligned — `max_align` is computed and never used
ld_tls.c:92-102 assigns each module `tls_offset = align_up(cursor +
memsz, align)`, making each offset a multiple of its *own* alignment.
But the slot address is `tp - tls_offset`, and `tp = block +
final_cursor` (ld_tls.c:133) — so a slot is aligned only if `tp` is
aligned to that module's alignment.  `final_cursor` is only aligned to
the *last* module's alignment; `max_align` (ld_tls.c:93,101) is dead.

Failure scenario: module 1 align=16 memsz=16 (offset 16), module 2
align=4 memsz=4 (offset 20) → tp = block+20 → module 1's slot at
block+4: 16-byte requirement, 4-byte reality.  Any SSE-aligned TLS
object (compiler-assumed `aligned(16)`) faults or silently misbehaves.
Fix: `cursor = align_up(cursor, max_align)` after the layout loop
(block is page-aligned, offsets are per-module-aligned, so an aligned
tp makes every slot aligned).  Same fix applies to `__ldso_alloc_tls`
via the cached `ld_tls_cursor`.

## Medium

### LDSO-03: SONAME dedup is dead code — dedup is actually by requested name
load_from_path sets `o->name` to the path basename (ld_load.c:350)
*before* calling `ld_cache_dynamic`, whose SONAME store is guarded by
`o->name[0] == '\0'` (ld_load.c:177) — never true.  The comment ("SONAME
will overwrite it") is wrong.  Consequences: `find_loaded` keys on
basename/request-string, so `dlopen("/opt/foo/libx.so")` after
`libx.so.1` was pulled in via DT_NEEDED (or a symlinked soname alias)
loads a **second copy** — duplicate globals, double constructors, pool
slots burned.  Fix: let SONAME overwrite the placeholder (drop the
guard or set the name after `ld_cache_dynamic`), and additionally
remember the resolved path for path-keyed dedup.

### LDSO-04: the executable's DT_VERSYM/DT_VERNEED are never cached
The field-by-field prog_obj setup in ld_main.c:333-366 has no
`DT_VERSYM` / `DT_VERNEED` / `DT_VERNEEDNUM` cases, so
`importer_version_hash()` (ld_reloc.c:34) sees `versym == NULL` for the
program and every one of the executable's imports resolves
*unversioned*.  Works while each name's default version is the wanted
one; silently mis-binds the moment a program legitimately needs a
non-default compat version (libstdc++ ships many).  Fix: cache the
three tags for prog_obj like ld_cache_dynamic does.

### LDSO-05: dladdr's symbol walk reads far past the symbol table
`dl_find_sym_in_obj` (ld_dl.c:204-243) iterates up to **65536**
Elf32_Syms with only an "8 consecutive all-zero entries" bail-out.  For
a small DSO (few hundred symbols, image a handful of pages) the walk
reinterprets strtab bytes as symbols — rarely zero — and marches ~1 MB
past the mapping → SIGSEGV inside dladdr.  Fix: bound by `DT_HASH`'s
nchains when present, else derive the count from the GNU-hash chain
end, else validate `st_name < strsz` per entry.

### LDSO-06: zero thread-safety in the runtime dl API
No lock anywhere: `__ldso_dlopen` mutates the object list
(`ld_obj_append` is a two-store update), `ld_relocate` flips guards,
`g_err`/`g_err_pending` is one global slot, and every resolver walks
the list lock-free.  Concurrent `dlopen`/`dlsym` from two threads —
routine in TDE/X apps, and now genuinely parallel with SMP — can
observe a half-linked list node or race the relocation guard.  Fix: a
single static mutex (futex) around dlopen/dlclose, plus
publish-after-init ordering for `ld_obj_append` (write `o->next = 0`
+ fields, then a release-store of `tail->next`).

### LDSO-07: dlopen failure leaves half-loaded objects in the global scope
`__ldso_dlopen` (ld_dl.c:87-102): transitive DT_NEEDED load failures
are discarded (`(void)ld_load_object(soname)`), then if `ld_relocate`
fails the function returns NULL — but the new objects **stay on the
list**, unrelocated and resolvable.  A later dlsym/relocation can bind
to an unrelocated library's symbols (garbage pointers), and the pool
slots leak.  Also inconsistent: the startup BFS dies loudly on a
missing dep; dlopen ignores it until relocation trips over the missing
symbols.  Fix: unlink the objects appended since entry on any failure
path (tail snapshot), and fail fast on transitive-load failure.

### LDSO-08: no W^X, no RELRO, and failed loads leak address space
Every file-backed PT_LOAD is mapped `prot | LD_PROT_WRITE`
(ld_load.c:271-275) and never re-protected — text of every DSO stays
RWX for the process lifetime; PT_GNU_RELRO is ignored.  There is no
munmap wrapper at all (ld_io.c), so every load_from_path failure after
the span reservation (ld_load.c:252) leaks the whole reservation, and
`__ldso_free_tls` can only leak (ld_tls.c:261, documented).  Fix: add
SYS_munmap/SYS_mprotect wrappers; mprotect each segment to its real
prot after `ld_relocate` (keeping +W only for DT_TEXTREL objects), and
apply PT_GNU_RELRO; munmap the reservation on failure paths.

## Low

### LDSO-09: dlsym/dlclose blindly cast the caller's handle
ld_dl.c:161-164 and :177-195 treat any non-sentinel `void *` as an
`ld_obj_t *`.  A stale or garbage handle wild-derefs.  Fix: validate
the handle by walking the object list (it's ≤192 entries).

### LDSO-10: lookup_sysv has no nchains bound or cycle guard
ld_resolve.c:135 walks `chains[idx]` unbounded; the twin in
ld_dl.c:341 checks `idx < nchains`.  A truncated/corrupt DSO hash
section → OOB reads or an infinite loop.  Fix: mirror the ld_dl.c
bound and add an iteration cap.

### LDSO-11: dlclose runs finalizers with no refcounting
ld_dl.c:177 — `dlclose(h)` fires DT_FINI immediately even when other
live objects DT_NEEDED it, and repeated dlopen/dlclose of the same lib
runs fini after the first close only (`finalized` latches).  Documented
as notional; worth a per-object refcount even without unmapping.

### LDSO-12: dlopen(NULL) handle searches only the executable
`__ldso_dlopen(NULL)` returns the prog_obj handle and `__ldso_dlsym`
on an object handle searches just that object (ld_dl.c:161).  POSIX
semantics for the main-program handle are global-scope search.  Fix:
treat the head-of-list handle like RTLD_DEFAULT.

### LDSO-13: per-thread TLS blocks leak on every thread exit
`__ldso_free_tls` is a no-op (ld_tls.c:261-268).  A create/join loop
leaks ≥1 page per thread, unbounded unless libpthread recycles slots.
Fix folds into LDSO-08's munmap wrapper.

### LDSO-14: BSS anonymous-map failure ignored
ld_load.c:299-303 `(void)a;` — if the anon BSS mapping fails, the load
"succeeds" and the process faults later touching .bss.  Fix: fail the
load.

### LDSO-15: DTPMOD32 always binds to the requesting object's module id
ld_reloc.c:200-212 (acknowledged in the comment): a GD reference to a
`__thread` variable *defined in another DSO* gets the importer's modid
→ wrong module's block.  DTPOFF32 at least errors on undef.  Fix:
resolve undef-sym DTPMOD32 via `ld_resolve_tls` to the defining module.

### LDSO-16: R_386_COPY doesn't check the destination's size
ld_reloc.c:134-155 copies `src.st_size` bytes without comparing against
the executable's own symbol size — a version-skewed library with a
grown object overruns the program's .bss slot.  glibc warns on
mismatch.  Fix: `min(dst.st_size, src.st_size)` + warn.

### LDSO-17: %esp-swap syscall wrappers vs. signal delivery
ld_io.c:29-97 point %esp at a 4-7 word local array during `int $0x80`.
A signal delivered mid-syscall (possible once the app has handlers and
calls dlopen/dlsym at runtime) pushes the sigframe *below* that array —
inside the live frame, clobbering neighboring locals.  Narrow, but the
failure would be baffling.  Fix: place the arg block at the *bottom* of
the frame (alloca-style below all locals), or block signals around the
swap, or pad below the array.

### LDSO-18: AT_SECURE doesn't gate LD_DEBUG / LD_TRACE_LOADED_OBJECTS
ld_main.c:237 drops only LD_PRELOAD.  Trace mode on a setuid binary
prints the load map and exits (behavior channel), LD_DEBUG leaks layout
addresses.  Fix: clear both when `a.secure`.

### LDSO-19: empty-valued env vars don't trigger
`LD_TRACE_LOADED_OBJECTS=` (empty value) is ignored (ld_main.c:218
requires a non-empty value); glibc triggers on presence.  Cosmetic.

### LDSO-20: dead instruction in ld_start.S
ld_start.S:42 `leal _DYNAMIC@GOTOFF, %ecx` — leftover from an abandoned
approach; the result is immediately overwritten by the .Lpc2 sequence.
Harmless; delete for clarity.

## Process note
The Jun-18-stale `/sbin/ld.so` incident (TDE libtdeinit "not found")
happened because `sbin/ld.so` is built by `build-rootfs.sh --dist` but
`--image` bakes whatever `dist/` already holds.  Not an ld.so bug, but
worth remembering: after touching ld.so, `make install
DESTDIR=$TOP/dist` before `--image`.
