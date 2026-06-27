# spandsp on Substrate

Cross-build of **spandsp 0.0.6** — a DSP library for telephony.  PsyMP3 uses it
for the **G.722** sub-band ADPCM codec (`PKG_CHECK_MODULES([G722],[spandsp])`,
links `-lspandsp`).

`fetch.sh` pins the release tarball + SHA-256 and applies the patch series;
`build.sh` cross-builds as a linux host (CC stays the substrate cross gcc) so
libtool emits the shared library, stamps OSABI 0x40, and mirrors the result
(`libspandsp.{a,so.2.0.0}` + headers + `spandsp.pc`) into the cross sysroot.

## What is disabled, and why

Upstream spandsp builds a single monolithic `libspandsp` that includes the FAX
modem stack (T.4 image coding, T.30 fax protocol, T.38 IP-fax gateway/terminal,
`image_translate`).  Those translation units `#include <tiffio.h>` and link
`-ltiff`, and `configure` hard-fails ("Cannot build without libtiff") when
libtiff is absent — which it is on substrate.

PsyMP3 only needs the G.722 codec, which is entirely self-contained (`g722.c`
pulls in no tiff/jpeg).  So patch `0001` strips the tiff/jpeg-dependent sources
from the library and makes the libtiff probe non-fatal, keeping every codec and
DSP primitive (G.711, G.722, G.726, GSM 06.10, LPC10, IMA/OKI ADPCM, the V.x
modems, tone generation/detection, echo cancel, ...).

`0001-substrate-strip-fax-tiff-jpeg-keep-core-codecs.patch`:

- `configure` — replace the fatal `as_fn_error "Cannot build without libtiff"`
  with a warning (build continues; `HAVE_LIBTIFF` stays undefined).
- `src/Makefile.in` + `src/Makefile.am` — drop the 11 tiff/jpeg-pulling sources
  (`fax.c image_translate.c t4_rx.c t4_tx.c t30.c t30_api.c t30_logging.c
  t31.c t38_core.c t38_gateway.c t38_terminal.c`) from `libspandsp_la_SOURCES`
  and from the pre-expanded `am_libspandsp_la_OBJECTS` list.  `fax_modems.c`,
  `t35.c`, `t38_non_ecm_buffer.c` are kept (no tiff/jpeg dependency).
- `src/spandsp.h.in` — drop the umbrella `#include <tiffio.h>` and the fax/T.x
  sub-includes, so a consumer's `#include <spandsp.h>` compiles without
  `tiffio.h` present and exposes no API for code that isn't linked in.
- `spandsp.pc.in` — drop `-ltiff` from `Libs.private` (a static link must not
  pull a nonexistent libtiff).

## Build notes (cross-compile gotchas, handled in build.sh)

- **maintainer mode**: there is no `AM_MAINTAINER_MODE`, so editing `Makefile.am`
  / `spandsp.h.in` makes them look newer than the generated `Makefile.in` /
  `configure` and triggers an automake/autoconf rerun we don't have.  build.sh
  touches the generated outputs (newest, in dependency order) to keep those
  rules dormant.
- **`rpl_malloc`**: cross-compiling, `AC_FUNC_MALLOC` can't run its probe and
  assumes a broken malloc, `#define malloc rpl_malloc` — which breaks the
  host-built `make_at_dictionary` codegen tool.  build.sh pre-seeds
  `ac_cv_func_{malloc,realloc}_0_nonnull=yes` (substrate's `malloc(0)` returns a
  unique pointer, glibc/musl-style, so the replacement is unnecessary).

## Verification

- `libspandsp.so.2.0.0` OSABI byte (offset 7) == `0x40` (ELFOSABI_SUBSTRATE).
- `g722_{encode,decode,encode_init,decode_init,encode_release,decode_release,
  encode_free,decode_free}` all exported (`nm -D`).
- A G.722 encode/decode test program cross-links dynamically against the staged
  `-lspandsp` via `pkg-config`.
