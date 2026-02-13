# Release Checklist

1. Update version in `usr.lib/regex/regex.pc`.
2. Run `make -C usr.lib/regex`.
3. Run tests under `tests/usr.lib/regex/`.
4. Run benchmarks under `usr.lib/regex/bench/`.
5. Update `ARCHITECTURE.md` if API or behavior changed.
