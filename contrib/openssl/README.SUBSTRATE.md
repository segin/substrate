# OpenSSL on substrate

Upstream:  https://www.openssl.org/
Pinned:    3.0.13  (LTS branch, released 2024-01-30)
Tarball:   `https://www.openssl.org/source/openssl-3.0.13.tar.gz`
SHA-256:   `88525753f79d3bec27d2fa7c66aa0b92b3aa9498dafd93d7cfa4b3780cdae313`

## Why

Substrate currently has no TLS/SSL stack.  Pulling in OpenSSL 3.0.x
(LTS) gives us:

- `libcrypto.so` — hashes, AEAD ciphers, RNG seeding, X.509 parsing,
  ASN.1, BN big-integer math — used by future SSH/HTTPS/IMAP/POP3
  client utilities and (eventually) by getrandom backends and
  password-hash modernization (Argon2 via libcrypto's HKDF).
- `libssl.so`    — TLS 1.2/1.3 client + server, useful once we have
  network utilities that need to speak HTTPS / IMAPS / SMTPS.
- `openssl(1)`   — command-line for cert generation, hashing,
  PEM/DER inspection.  Convenient on-image.

## Scope

The default 3.0.x build will produce libcrypto.so.3, libssl.so.3,
and the `openssl` driver.  Substrate target needs the usual
"target-clang / linux-generic32" Configure recipe with
`no-asm`, `no-engine`, `no-tests`, and an explicit `--cross-compile-
prefix=i386-unknown-substrate-`.

## Layout

    contrib/openssl/
        README.SUBSTRATE.md   ← this file
        fetch.sh              ← download + verify + extract + patch
        build.sh              ← Configure + make + staged install
        series                ← patch order (empty until needed)
        patches/              ← substrate-specific patches
        build/                ← extracted source + build (NOT vendored)
