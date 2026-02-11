# Substrate Regex Library

This is the system regex library for Substrate. It provides a safe default engine
with deterministic matching and strict resource limits.

## Build

From the project root:

```
make
```

To build only the library:

```
make -C usr.lib/regex
```

Optional adapters:

```
make -C usr.lib/regex USE_PCRE2=1
make -C usr.lib/regex USE_RE2=1 DEFAULT_ENGINE_RE2=1
make -C usr.lib/regex USE_ICU=1
```

When enabling ICU, link consumers with ICU libraries (typically `-licuuc`).
PCRE2/RE2 adapters require their respective development headers and libraries.
Streaming is provided by the safe engine; adapters may return `REGEX_ERR_UNSUPPORTED`
for iterator APIs.

## Install

```
make install
```

This installs `libregex.a`, headers, and man pages into `dist/` by default.

## Tests

```
make -C tests/usr.lib/regex
make -C tests/usr.lib/regex run
```

CI runners live in `tests/ci/`.

## Security

The default engine uses a DFA prefilter and a bounded NFA capture pass with
configurable limits to avoid catastrophic backtracking. Use `regex_set_limits`
if you need tighter caps.
