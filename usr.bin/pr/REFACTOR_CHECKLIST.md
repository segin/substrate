# pr Refactor Checklist

- [x] Ensure page headers include timestamp, selected header/file name, and page number.
- [x] Use multibyte-aware display width handling with `wcwidth(3)` instead of byte counts.
- [x] Add regression tests for by-column and across column layouts.
- [ ] Add full POSIX option grammar coverage (`+page`, `-e`, `-i`, `-o`) if required by future compatibility goals.
- [ ] Add golden output fixtures for locale-dependent header formatting.
