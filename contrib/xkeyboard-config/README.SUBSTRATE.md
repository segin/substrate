# xkeyboard-config 2.36 — substrate port

XKB layout descriptions: rules, symbols, keycodes, types, compat,
geometry.  Consumed by xkbcomp at runtime when the X server loads a
keymap.

## Build

    cd contrib/xkeyboard-config
    ./fetch.sh
    ./build.sh

Stages dist-xkeyboard-config/usr/share/X11/xkb/{rules,symbols,keycodes,
types,compat,geometry}/.

## Substrate-specific

* 2.36 picked deliberately as the last release before the project
  went meson-only.  We don't have a cross-meson chain yet, so the
  port just stages the upstream data tree verbatim — the rules /
  symbols / keycodes files are plain text that xkbcomp parses at
  runtime, no compile step needed.
* Locale-translated layout descriptions (.po -> .gmo) are skipped.
