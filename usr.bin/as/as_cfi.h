#ifndef SUBSTRATE_AS_CFI_H
#define SUBSTRATE_AS_CFI_H

/*
 * Call frame information: the `.cfi_*` directives of a source made the
 * CIEs and FDEs of `.eh_frame`, or of `.debug_frame`.
 *
 * The emitter says where each directive stands -- its section and the
 * offset in it -- and is handed back the bytes of the section and the
 * places in them that a relocation fills.
 */

#include "as_parser.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const as_directive_t *d;
    const char *section; /* the section the directive is written in */
    uint64_t offset;     /* and where in it */
    const char *file;
    unsigned line;
} as_cfi_event_t;

typedef struct {
    size_t offset;      /* in the frame section */
    unsigned width;     /* 2, 4 or 8 bytes */
    int pcrel;          /* the value less the field's own address */
    const char *symbol; /* a symbol's name, or a section's */
    int64_t addend;
} as_cfi_fixup_t;

typedef struct {
    uint8_t *bytes;
    size_t len;
    as_cfi_fixup_t *fixups;
    size_t fixup_count;
} as_cfi_frames_t;

/* Which sections the source asked for with `.cfi_sections`; with none
 * said, `.eh_frame`. */
void as_cfi_sections(const as_cfi_event_t *events, size_t count, int *eh_frame, int *debug_frame);

/*
 * The frames of `events`, which are every `.cfi_*` directive of the
 * source in its order: for `.eh_frame`, or with `debug_frame` for
 * `.debug_frame`.  0, or -1 with the reason in errbuf, which names the
 * file and line.
 */
int as_cfi_build(const as_cfi_event_t *events, size_t count, int is_64, int debug_frame, as_cfi_frames_t *out,
                 char *errbuf, size_t errbuf_sz);

void as_cfi_frames_free(as_cfi_frames_t *frames);

#ifdef __cplusplus
}
#endif

#endif
