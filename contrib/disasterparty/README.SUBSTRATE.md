# disasterparty 0.6.0 — substrate port

[disasterparty](https://github.com/segin/disasterparty) is a C client library
for LLM chat-completion APIs (OpenAI, Anthropic, Google Gemini) built on
libcurl + libcjson.  Ported here as the backend for `motifgpt`.

## Build

An autotools + libtool project, but the GitHub source archive ships no
generated `configure`, so `fetch.sh` runs `autoreconf --force --install` on the
build host after extraction (and re-teaches the freshly installed `config.sub`
the `*-substrate*` triplet).

`build.sh` then cross-configures with `PKG_CONFIG_LIBDIR` pinned at the staged
`dist-cjson`, `dist-curl`, `dist-openssl`, and `dist-zlib` trees so the
`libcurl`/`libcjson` probes — and the eventual link — never see host libraries.
`--disable-shared` (substrate's libtool produces static archives only), and only
the library, header, `.pc`, and man pages are installed; the `tests/` subdir
links programs that talk to live API endpoints and is out of scope for a cross
build.

## Dependencies

- `libcurl` (`contrib/curl`, which pulls `openssl` + `zlib`)
- `libcjson` (`contrib/cjson`)

## Layout

    fetch.sh   download + sha256 verify + extract + autoreconf + config.sub fixup
    build.sh   cross-configure + make -C src + install lib/header/.pc/man
    series     (empty — no source patches needed)

Staged output:
`dist-disasterparty/usr/{lib/libdisasterparty.a,lib/pkgconfig/disasterparty.pc,include/disasterparty/disasterparty.h,share/man}`.

The installed `disasterparty.pc` carries `Requires: libcurl, libcjson`, so a
consumer's `pkg-config --libs disasterparty` resolves the whole chain
(`-ldisasterparty -lcurl -lssl -lcrypto -lcjson`).
