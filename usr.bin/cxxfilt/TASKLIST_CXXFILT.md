# `usr.bin/cxxfilt` Tasklist

Goal: implement `c++filt`-compatible demangler utility.

- [ ] Implement Itanium ABI demangling baseline.
- [ ] Provide stdin/argv streaming modes with stable output.
- [ ] Add style toggles and failure fallback behavior.
- [ ] Add tests with large symbol corpora and malformed names.
- [ ] Integrate as optional helper for `nm/objdump/addr2line`.
