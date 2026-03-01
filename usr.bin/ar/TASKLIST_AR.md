# `usr.bin/ar` Tasklist

Goal: implement a full‑featured `ar(1)` archive utility using `libelfobj` for symbol table generation.

---

## 1. Archive Format Support

### 1a. GNU Format (default)
- [ ] Write `!<arch>\n` magic.
- [ ] Short names: `name/` padded to 16 bytes.
- [ ] Long name table (`//`): build string table of names >15 chars or containing spaces.
- [ ] Long name references: `/<offset>` entries pointing into `//` table.
- [ ] Parse `//` table on read; strip trailing `/` and `\n` from entries.

### 1b. BSD Format
- [ ] Extended names via `#1/<length>` prefix in `ar_name` field.
- [ ] Name bytes prepended to member data; `ar_size` includes name length.
- [ ] On read: detect `#1/` prefix, consume name from data, adjust payload size.

### 1c. Format Auto‑Detection
- [ ] On read: detect GNU vs BSD by examining first member (`#1/` → BSD; `/` or `//` → GNU).
- [ ] `--format=bsd` / `--format=gnu`: force output format.
- [ ] Default output format: GNU.

### 1d. Thin Archives
- [ ] `!<thin>\n` magic recognition.
- [ ] `-T` modifier: create thin archive.
- [ ] Store member path as payload instead of member data.
- [ ] On extract/print: open external file via stored path.
- [ ] On read: set `thin_ref` flag, store `thin_path`.
- [ ] Symbol table generation skipped for thin archives.

## 2. Operations

### 2a. `r` — Replace / Insert
- [ ] If member exists: replace data, update header fields (mtime, uid, gid, mode, size).
- [ ] If member doesn't exist: append new member.
- [ ] With `-u`: skip replacement if archive member is newer or same age.
- [ ] With `-a <name>`: insert after position member.
- [ ] With `-b`/`-i <name>`: insert before position member.
- [ ] Fallback to append if position member not found (with warning).
- [ ] Support `-c`: suppress "creating archive" warning when archive doesn't exist.
- [ ] With `-s`: rebuild symbol table after insert.

### 2b. `q` — Quick Append
- [ ] Append files to end of archive without checking for duplicates.
- [ ] `-u` incompatible with `q` (error).
- [ ] With `-s`: rebuild symbol table after append.

### 2c. `d` — Delete
- [ ] Mark matching members as deleted.
- [ ] With `-N <count>`: delete only the Nth instance of the named member.
- [ ] Warn if named member not found.
- [ ] With `-v`: print `d - <name>` for each deletion.
- [ ] With `-s`: rebuild symbol table after delete.

### 2d. `m` — Move
- [ ] Unlink member from current position, reinsert.
- [ ] Default: move to end (tail).
- [ ] With `-a <name>`: move after position member.
- [ ] With `-b`/`-i <name>`: move before position member.
- [ ] Error if position member not found.
- [ ] No‑op if target is the position member itself.
- [ ] With `-v`: print `m - <name>`.
- [ ] With `-s`: rebuild symbol table after move.

### 2e. `t` — List (Table of Contents)
- [ ] Print member names, one per line.
- [ ] Skip internal members (symbol table, `//` name table).
- [ ] Filter to named members if arguments given.
- [ ] With `-N <count>`: match only the Nth instance.
- [ ] With `-v`: full `ls -l`‑style output (type+permissions, uid/gid, size, date, name).

### 2f. `x` — Extract
- [ ] Write member data to file named after member.
- [ ] Refuse extraction if member name contains path components (`/`, `..`).
- [ ] Set file permissions from `ar_mode` (octal).
- [ ] With `-o`: preserve original mtime via `utimes()`.
- [ ] With `-C`: no‑clobber mode (`O_EXCL`).
- [ ] With `-N <count>`: extract only the Nth instance.
- [ ] With `--no-same-owner`: skip `chown()` even when running as root.
- [ ] Root‑only: restore uid/gid via `chown()` (unless `--no-same-owner`).
- [ ] With `-v`: print `x - <name>`.
- [ ] Thin archive: read data from external file path.

### 2g. `p` — Print
- [ ] Write member data to stdout.
- [ ] Skip symbol table members.
- [ ] Filter to named members if arguments given.
- [ ] With `-v`: print `p - <name>` prefix.
- [ ] Thin archive: stream data from external file.

### 2h. `s` — Symbol Table Only (ranlib mode)
- [ ] When `s` is the only operation: rebuild symbol table and rewrite archive.
- [ ] With `-S`: strip symbol table instead of rebuilding.

## 3. Ranlib Alias

- [ ] Detect invocation as `ranlib` via `argv[0]` basename matching.
- [ ] `ranlib <archive>`: equivalent to `ar s <archive>`.
- [ ] `ranlib -t <archive>`: touch symbol table timestamp without full rebuild.
- [ ] No other flags accepted in ranlib mode.

## 4. Symbol Table Generation

### 4a. `libelfobj` Integration
- [ ] Open each member's data via `elf_open_memory_with_options(data, size, ELFOBJ_OPEN_NOCOPY)`.
- [ ] Iterate symbols: `elf_symbol_count()` / `elf_symbol_at()`.
- [ ] Collect symbols with `STB_GLOBAL` or `STB_WEAK` binding.
- [ ] Skip `STT_FILE` and `STT_SECTION` type symbols.
- [ ] Skip `SHN_UNDEF` symbols (undefined references).
- [ ] Include `SHN_COMMON` symbols.
- [ ] Use `elf_symbol_name()` for the string.
- [ ] `elf_close()` every handle.
- [ ] Silently skip non‑ELF members (no error).
- [ ] Handle ELF32 and ELF64 members transparently.

### 4b. GNU Symbol Table (`/`)
- [ ] Big‑endian uint32 count, followed by uint32 offset array, followed by NUL‑terminated string pool.
- [ ] Sort symbols alphabetically by name (stable by member position for ties).
- [ ] Offsets: file position of each member's `ar_hdr` from start of archive.
- [ ] Compute offsets via two‑pass: calculate sizes first, then fill.

### 4c. GNU SYM64 Table (`/SYM64/`)
- [ ] Emit when any member offset exceeds 2³² (archives > 4 GiB).
- [ ] Big‑endian uint64 count + uint64 offset array + string pool.

### 4d. BSD Symbol Table (`__.SYMDEF SORTED`)
- [ ] uint32 ranlib array size, followed by `struct ranlib` entries, followed by uint32 string pool size, followed by string pool.
- [ ] `ran_strx`: offset into string pool. `ran_off`: file offset of member header.
- [ ] Sorted by symbol name.

### 4e. Symbol Table Lifecycle
- [ ] `drop_symbol_tables()`: remove all existing symtab/nametable members.
- [ ] `ranlib()`: drop old tables, collect symbols, allocate new table, insert as first member.
- [ ] `touch_symbol_table_member()`: update timestamp of existing symbol table without rebuild.
- [ ] Symbol table skipped for thin archives.

## 5. Modifiers

- [ ] `-v` (`MOD_VERBOSE`): announce each operation (a/r/d/m/x/p prefix).
- [ ] `-u` (`MOD_UPDATE`): with `r`, skip if archive member is not older.
- [ ] `-a` (`MOD_AFTER`): position — insert after named member.
- [ ] `-b` / `-i` (`MOD_BEFORE`): position — insert before named member.
- [ ] `-a` and `-b`/`-i` mutually exclusive (error if both).
- [ ] `-o` (`MOD_PRESERVE`): on extract, preserve original modification time.
- [ ] `-N <count>` (`MOD_COUNT`): operate on Nth instance of multiply‑occurring names.
- [ ] `-l` (`MOD_LOCAL`): accepted, no‑op (SysV compat).
- [ ] `-C` (`MOD_NOCLOBBER`): on extract, fail if output file exists (`O_EXCL`).
- [ ] `-D` / `-U` (`MOD_DETERMINISTIC`): zero uid/gid/mtime, mode 0644.
- [ ] `-S` (`MOD_NOSYMTAB`): suppress symbol table generation.
- [ ] `-T` (`MOD_THIN`): produce thin archive output.
- [ ] `-c` (`AR_CREATE`): suppress "creating archive" warning.
- [ ] `--no-same-owner`: skip ownership restoration on extract.

## 6. Deterministic Mode

- [ ] With `-D`/`-U`: set uid=0, gid=0, mtime=0, mode=0100644 in all member headers.
- [ ] Honor `SOURCE_DATE_EPOCH` environment variable as override timestamp.
- [ ] Honor `ZERO_AR_DATE` environment variable (set mtime to 0).
- [ ] Without deterministic mode or env vars: use source file's mtime.

## 7. Archive I/O

### 7a. Reading (`read_archive`)
- [ ] Validate `!<arch>\n` or `!<thin>\n` magic (8 bytes).
- [ ] Parse each `ar_hdr` (60 bytes); validate `ar_fmag`.
- [ ] Validate numeric fields: `ar_size` (decimal), `ar_uid` (decimal), `ar_gid` (decimal), `ar_mode` (octal).
- [ ] Handle truncated members (size exceeds remaining file).
- [ ] Build in‑memory linked list of `struct ar_memb`.
- [ ] BSD extended name: read name bytes from data region.
- [ ] GNU extended name: dereference `/<offset>` into `//` string table.
- [ ] Recognize special members: `/`, `//`, `/SYM64/`, `__.SYMDEF`, `__.SYMDEF SORTED`.
- [ ] Even‑byte alignment: skip padding byte between members.

### 7b. Writing (`write_archive`)
- [ ] Write to temporary file (`<path>.tmp.<pid>`).
- [ ] Write magic.
- [ ] Build GNU `//` name table if any member has name >15 chars or contains spaces.
- [ ] Write symbol table as first member.
- [ ] Write `//` name table before first data member.
- [ ] Write each non‑deleted member: header + extended name (if BSD) + data + padding.
- [ ] Apply deterministic processing to each written header.
- [ ] Two‑pass symbol offset calculation: determine member positions, then fill offsets.
- [ ] `rename()` temp over original on success.
- [ ] `unlink()` temp on failure.

## 8. Data Structures

### 8a. `ar.h`
- [ ] `ARMAG`, `SARMAG`, `THINMAG`, `ARFMAG` constants.
- [ ] `struct ar_hdr` (60 bytes): `ar_name[16]`, `ar_date[12]`, `ar_uid[6]`, `ar_gid[6]`, `ar_mode[8]`, `ar_size[10]`, `ar_fmag[2]`.
- [ ] `RANLIBMAG`, `RANLIBSORT` strings.
- [ ] `struct ranlib`: union `ran_un` {`ran_strx`, `*ran_name`} + `ran_off`.

### 8b. `struct ar_memb` (internal)
- [ ] `hdr`: raw archive header.
- [ ] `name`: parsed member name.
- [ ] `data`: member payload (NULL for thin refs).
- [ ] `thin_path`: external file path (thin archive).
- [ ] `size`: payload size.
- [ ] `gnu_name_off`: offset into `//` table.
- [ ] `gnu_name_ref`: whether this member uses long name reference.
- [ ] `dirty`, `deleted`, `thin_ref`: state flags.

### 8c. `struct sym_entry`
- [ ] `name`: symbol name string.
- [ ] `offset`: file offset of member header.
- [ ] `member`: pointer to `ar_memb` for position‑based sorting.

## 9. Error Handling

- [ ] Non‑archive input: "file format not recognized", exit 1.
- [ ] Missing archive for `d`/`m`/`t`/`x`/`p`: error with `errno`.
- [ ] Missing archive for `r`/`q`: create new archive (with `-c` suppress warning).
- [ ] Member not found for `d`/`m`: warn per member.
- [ ] Path‑traversal member names on extract: refuse with warning.
- [ ] Truncated archive data: warn, skip remainder.
- [ ] Invalid numeric header fields: warn, skip member.
- [ ] `malloc()` failure: fatal error.
- [ ] Write failure: unlink temp, fatal error with `errno`.
- [ ] Invalid option combination (`-u` + `q`; `-a` + `-b`): fatal error.
- [ ] Multiple operations specified: fatal error.

## 10. Build System

- [ ] `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/ar`.
- [ ] `install-ranlib-link`: symlink `ar` → `ranlib` in `$(DESTDIR)/usr/bin/`.
- [ ] `install-man`: install `ar.1` and `ranlib.1` to `$(DESTDIR)/usr/man/man1/`.
- [ ] `libelfobj.a` dependency: auto‑build via recursive make.

## 11. Testing

### 11a. Basic Operation Tests
- [ ] `ar rc lib.a a.o b.o`: create archive, verify member list with `ar t`.
- [ ] `ar r lib.a a.o` with updated `a.o`: member data replaced.
- [ ] `ar d lib.a b.o`: member removed, `ar t` confirms.
- [ ] `ar q lib.a c.o`: quick append, member present at end.
- [ ] `ar x lib.a a.o`: extracted file matches original.
- [ ] `ar x lib.a` (no args): extract all members.
- [ ] `ar p lib.a a.o`: stdout matches member data.
- [ ] `ar m lib.a a.o`: member moved to end.

### 11b. Position Modifier Tests
- [ ] `ar rba b.o lib.a new.o`: `new.o` appears before `b.o` in listing.
- [ ] `ar ra a.o lib.a new.o`: `new.o` appears after `a.o` in listing.
- [ ] `ar ma b.o lib.a a.o`: `a.o` moved after `b.o`.

### 11c. Symbol Table Tests
- [ ] `ar rcs lib.a a.o b.o`: archive has GNU `/` symbol table as first member.
- [ ] Symbol table contains only `STB_GLOBAL` and `STB_WEAK` defined symbols.
- [ ] No `STT_FILE`, `STT_SECTION`, or `SHN_UNDEF` symbols in table.
- [ ] `ar rcs --format=bsd lib.a a.o`: archive has `__.SYMDEF SORTED` table.
- [ ] Symbols sorted alphabetically.
- [ ] Round‑trip: `ar rcs` → `ld` link → executable runs.
- [ ] `-S` modifier: archive written without symbol table.
- [ ] `ranlib lib.a`: adds/rebuilds symbol table.
- [ ] `ranlib -t lib.a`: only timestamp updated.
- [ ] `SHN_COMMON` symbol: included in symbol table.

### 11d. Extended Name Tests
- [ ] GNU: member with 20‑char name → `//` table + `/<offset>` reference.
- [ ] BSD: member with 20‑char name → `#1/20` prefix.
- [ ] Names containing spaces: handled via long name mechanism.
- [ ] Round‑trip: create with long names → extract → names preserved.

### 11e. Thin Archive Tests
- [ ] `ar rcTs thin.a a.o b.o`: creates `!<thin>\n` archive.
- [ ] `ar t thin.a`: lists members.
- [ ] `ar x thin.a a.o`: extracts from external file.
- [ ] No symbol table generated for thin archives.

### 11f. Modifier Tests
- [ ] `-v` on each operation: appropriate prefix printed (a/r/d/m/x/p).
- [ ] `-u`: skip replace when archive member same age or newer.
- [ ] `-N 2 -d lib.a foo.o`: only second instance of `foo.o` deleted.
- [ ] `-C` on extract: fails if file exists.
- [ ] `-o` on extract: extracted file mtime matches archive member mtime.

### 11g. Deterministic Tests
- [ ] `ar rcDs lib.a a.o`: uid=0, gid=0, mtime=0 in all member headers.
- [ ] Two identical `ar rcDs` invocations produce byte‑identical archives.
- [ ] `SOURCE_DATE_EPOCH=1234567890 ar rcs lib.a a.o`: mtime set to epoch value.
- [ ] `ZERO_AR_DATE=1 ar rcs lib.a a.o`: mtime set to 0.

### 11h. Error Handling Tests
- [ ] `ar r nonexistent.a a.o`: archive created with warning (or silent with `-c`).
- [ ] `ar d nonexistent.a foo.o`: error, exit 1.
- [ ] `ar x lib.a ../etc/passwd`: refused (path traversal).
- [ ] `ar xC lib.a a.o` with existing `a.o`: fails with error.
- [ ] `ar dqu lib.a a.o`: error (`-u` incompatible with `q`).
- [ ] `ar rt lib.a`: error (multiple operations).
- [ ] Corrupt archive (bad magic): "file format not recognized".

### 11i. Atomicity Tests
- [ ] Simulate write failure (disk full via `ulimit -f`): original archive untouched.
- [ ] Verify temp file cleaned up on failure.
- [ ] Successful write: `rename()` atomically replaces original.

### 11j. Integration Tests
- [ ] `cc -c a.c b.c` → `ar rcs lib.a a.o b.o` → `cc main.c -L. -llib -o main` → `main` runs.
- [ ] Substrate `ar` archive consumable by host `ld`.
- [ ] Host `ar` archive consumable by Substrate `ld`.

## 12. Man Page

- [ ] Write `ar.1` covering all operations, modifiers, format details, environment, and exit codes.
- [ ] Install to `$(DESTDIR)/usr/man/man1/ar.1`.
