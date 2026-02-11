# Refactoring Notes

## LibC Dependencies

The following libc functions were identified as missing or potentially missing in the build environment, and have been handled locally or noted:

*   `basename`: Implemented locally as `get_basename`.
*   `ar.h`: Implemented locally in `usr.bin/ar/ar.h`.
*   `ranlib.h`: Implemented locally in `usr.bin/ar/ar.h`.
*   `elf.h`: Used `sys/exec/formats/elf.h` with local fallbacks for missing definitions (`Elf32_Shdr`, `ELFMAG`, etc.).

## ELF Parsing

The symbol table generation logic (`ranlib`) parses ELF object files to extract global symbols.
*   It assumes 32-bit ELF (`Elf32_Ehdr`) as Substrate is 32-bit (i386).
*   It iterates over the section headers to find `SHT_SYMTAB`.
*   It extracts `STB_GLOBAL` and `STB_WEAK` symbols.

## Archive Format

*   We use the BSD format for long filenames (`#1/length` in name field, real name at start of data).
*   Standard filenames are space-padded.
*   Numeric fields are decimal, space-padded.

## TODO

*   Implement `m` (move) operation (currently parsed but logic not implemented fully - it falls through or does nothing?).
    *   Actually `do_move` was not implemented in `ar.c`. The switch case just does nothing?
    *   Check `ar.c`.

    ```c
    case AR_MOVE:
        /* operation |= AR_MOVE in main */
        /* switch(operation) ... case AR_MOVE: missing? */
    ```

    The switch in `main` has:
    ```c
    case AR_MOVE:
        /* ... */
        /* Logic missing in current ar.c? */
        /* I only implemented append, delete, extract, list, table. */
    ```

    The requirements asked for `m`.
    "Operations: r, c, s, t, x, d, m, q."
    I missed `m` implementation logic.

    I should implement `move_members` or similar.
    Move requires reordering the linked list.

    I will fix `ar.c` to implement `m` before submitting.

    `ar m [pos] archive member...`
    If no pos, move to end.
    With `a` (after), `b` (before), `i` (before).
    My `ar` argument parsing handles modifiers `a`, `b`, `i`?
    No, my `ar` usage says `usage: ar [drqtpmx][lsvV] archive [member...]`.
    It doesn't handle positioning args.
    Standard `ar` uses `a`, `b`, `i` modifiers which take an argument (the position member name).
    This complicates parsing.

    If `m` is required but positioning is complex, I will implement "move to end" as default.
    The prompt said "Operations: ... m ...".
    It didn't explicitly demand full positioning support, but it's implied by `m`.
    However, "move to end" is the basic `m`.

    I'll add `move_members` to `ar.c` which moves specified members to the end.
    Supporting `a`/`b` requires parsing arguments that might be flags or filenames.
    Standard `ar` handles `ar ma posname archive member`.

    Given the complexity and the "clean room" nature, I'll stick to basic `m` (move to end) and note it.
    Or if I have time, I'll fix it.
    The parsing logic currently:
    `char *key = argv[argi];`
    It doesn't handle `a`, `b`, `i` in the key string taking arguments.

    I'll implement basic `m` (move to end).

    I will update `ar.c`.
