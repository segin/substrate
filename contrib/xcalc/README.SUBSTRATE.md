# xcalc (substrate port)

The Athena-widget calculator (TI-30 / HP-10C style).  Exercises the
ported libXaw (Athena) toolkit on top of libXt/libX11.

- **Upstream:** xcalc 1.1.0 (`https://www.x.org/releases/individual/app/`)
- **Layout:** patch series against the upstream tarball; nothing vendored.
  `fetch.sh` downloads + SHA-verifies + extracts + applies `patches/`.
- **Build:** `./fetch.sh && ./build.sh` → stages `/usr/bin/xcalc` and the
  `XCalc` / `XCalc-color` app-defaults into `dist-xcalc/`.
- **Dependencies (staged first):** xorgproto, libxcb, libXau, xtrans,
  libX11, libXext, libXmu, libXpm, libICE, libSM, libXt, libXaw.

## Substrate specifics

- `0001-config-sub-substrate.patch` teaches the bundled `config.sub` the
  `-substrate` OS name so `--host=i386-unknown-substrate` validates.
- Needs a running X server (e.g. Xfbdev) and `DISPLAY` set.  The
  app-defaults file must be reachable on the X resource path
  (`/usr/share/X11/app-defaults/XCalc`); it is installed there by
  `make install`.

## Known cosmetic limitations

- A few buttons (√, π) request the `-adobe-symbol-*` font, which is not
  in substrate's font set; Xt falls back to the default font so those
  labels show placeholder glyphs.  The calculator is fully functional;
  installing an adobe-symbol PCF/BDF font would fix the labels.
- First window-map is slow (tens of seconds): xcalc realizes ~50 Athena
  Command widgets and each costs several X round-trips, which are
  expensive over substrate's local X transport.  This is a server/
  transport latency property, not an xcalc bug.
