# `usr.bin/strings` Tasklist

Goal: implement `strings` with optional ELF-aware scanning driven by `libelfobj`.

---

## 1. Raw Byte Scanning Mode (default)

- [ ] Scan input file byte‑by‑byte, extract sequences of printable characters ≥ min‑length.
- [ ] Default min‑length: 4 (configurable via `-n <min>` / `--bytes=<min>`).
- [ ] Printable character definition: ASCII 0x20–0x7E plus tab (0x09).
- [ ] Terminate a string at any non‑printable byte or EOF.
- [ ] Default: scan entire file.
- [ ] `-d` / `--data`: scan only initialized data sections (requires ELF mode, see §2).

## 2. ELF‑Aware Mode (`-d` / `--data`)

- [ ] Open file with `elf_open()`.
- [ ] If not ELF: fall back to raw mode silently (or warn, per GNU behavior).
- [ ] Iterate sections; select sections with `SHF_ALLOC` and `sh_type == SHT_PROGBITS`.
- [ ] Optionally include `SHT_NOTE` sections.
- [ ] Scan only the byte ranges of selected sections (using `elf_section_data()` + `elf_section_offset()` + `elf_section_size()`).
- [ ] `elf_close()` after scanning.

## 3. Encoding Modes (`-e <encoding>` / `--encoding=<encoding>`)

- [ ] `s` (default): single‑byte characters (ASCII/ISO‑8859).
- [ ] `S`: 8‑bit characters including high‑bit (all bytes 0x20–0xFF are printable).
- [ ] `b`: 16‑bit big‑endian (scan for UTF‑16BE sequences).
- [ ] `l`: 16‑bit little‑endian (scan for UTF‑16LE sequences).
- [ ] `B`: 32‑bit big‑endian (scan for UTF‑32BE sequences).
- [ ] `L`: 32‑bit little‑endian (scan for UTF‑32LE sequences).
- [ ] For multi‑byte encodings: extract sequences of ≥ min‑length characters, NUL‑terminated or broken by non‑printable code units.

## 4. Output Options

### 4a. Offset Prefix (`-t <format>` / `--radix=<format>`)
- [ ] `o`: octal file offset prefix.
- [ ] `x`: hexadecimal file offset prefix.
- [ ] `d`: decimal file offset prefix.
- [ ] No `-t`: no offset prefix (just the string).

### 4b. File Name Prefix
- [ ] `-f` / `--print-file-name`: prepend filename to each line.
- [ ] For multiple files: always prepend filename to disambiguate.

### 4c. Other
- [ ] `-a` / `--all` / `-`: scan the whole file, not just data sections (default anyway except with `-d`).
- [ ] `-w` / `--include-all-whitespace`: treat form‑feed, vertical tab, carriage return as printable.
- [ ] `-o`: equivalent to `-t o`.
- [ ] `-T <bfdname>` / `--target=<bfdname>`: ignored (GNU compat).

## 5. Multi‑File Support

- [ ] Accept multiple filenames on command line.
- [ ] Process each in order; prepend filename if >1 file or if `-f`.
- [ ] Accept `-` as stdin.
- [ ] Archive input (`.a`): iterate members, scan each, identify by `archive(member)`.

## 6. Error Handling

- [ ] Permission denied: warn, continue to next file.
- [ ] Directory argument: warn, skip.
- [ ] Binary read error: warn, output what was found so far.
- [ ] Invalid `-e` encoding: error, exit 1.
- [ ] Invalid `-n` value (0 or negative): error, exit 1.

## 7. Performance

- [ ] Use buffered I/O (16–64 KiB read buffer).
- [ ] For ELF mode: operate on memory‑mapped or read section data directly, avoid re‑reading.
- [ ] Avoid allocating for each found string — print directly to stdout.

## 8. Deterministic Output

- [ ] Strings are output in file‑offset order (natural scan order).
- [ ] Identical input always produces identical output (no randomness/timing sensitivity).

## 9. Build System

- [ ] Create `Makefile` linking `libelfobj.a` (optional dep for ELF‑aware mode).
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/strings`.

## 10. Testing

### 10a. Basic Extraction Tests
- [ ] File with known ASCII strings embedded in binary data — verify all extracted.
- [ ] Minimum length: `-n 8` skips 4–7 char strings.
- [ ] `-n 1` extracts single printable characters.
- [ ] Zero‑length file: no output.
- [ ] File with no printable sequences: no output.

### 10b. Encoding Tests
- [ ] `-e l`: extract UTF‑16LE strings from a PE/binary with embedded wide strings.
- [ ] `-e b`: extract UTF‑16BE strings.
- [ ] `-e S`: include high‑bit characters (0x80–0xFF).
- [ ] Encoding mismatch (e.g. `-e b` on LE data): no false positives above min‑length.

### 10c. ELF‑Aware Tests
- [ ] `-d` on ET_EXEC: only `.rodata` / `.data` section strings, not code bytes.
- [ ] `-d` on non‑ELF file: falls back to full scan.
- [ ] ELF with a `.comment` section (non‑alloc): excluded from `-d` scan.
- [ ] ELF32 and ELF64 inputs.

### 10d. Offset Tests
- [ ] `-t x`: verify hex offsets match actual file positions.
- [ ] `-t d`: verify decimal offsets.
- [ ] `-t o`: verify octal offsets.

### 10e. Multi‑File Tests
- [ ] Two files: output interleaved with filenames when `-f`.
- [ ] stdin (`-`): reads from pipe, no filename prefix unless `-f`.
- [ ] Archive input: per‑member output.

### 10f. Edge Cases
- [ ] String at exact EOF (no trailing NUL or non‑printable).
- [ ] String spanning a read‑buffer boundary (16 KiB+ long string).
- [ ] Binary file with only NUL bytes: no output.
- [ ] Very long string (100 KiB): output without truncation.

### 10g. Integration Tests
- [ ] `cc -o hello hello.c` → `strings hello` — contains `"Hello"` and libc strings.
- [ ] Compare output against host `strings` on same binary.

## 11. Man Page

- [ ] Write `strings.1` covering all flags, encoding modes, and exit codes.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
