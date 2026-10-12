#ifndef SUBSTRATE_AS_DWLINE_H
#define SUBSTRATE_AS_DWLINE_H

/*
 * The line table of `.debug_line`, from the `.file` and `.loc`
 * directives of a source.
 *
 * The emitter says what files there are and what rows -- each a place
 * in a section and the file, line and column that are there -- and is
 * handed back the section's bytes and the places in them a relocation
 * fills.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned number;
    const char *dir;  /* what `.file N "dir" "name"` gave before the name, or NULL */
    const char *name; /* as written: it may have directories in it */
} as_dwline_file_t;

#define AS_DWLINE_IS_STMT (1u << 0)
#define AS_DWLINE_BASIC_BLOCK (1u << 1)
#define AS_DWLINE_PROLOGUE_END (1u << 2)
#define AS_DWLINE_EPILOGUE_BEGIN (1u << 3)
/* `view -0`: the views are counted from this row, though the address
 * is the one the row before has. */
#define AS_DWLINE_VIEW_RESET (1u << 4)

typedef struct {
    const char *section;
    uint64_t offset;
    unsigned file;
    unsigned line;
    unsigned column;
    unsigned flags;
    unsigned isa;
    unsigned discriminator;
} as_dwline_row_t;

/* Where a section with rows in it ends. */
typedef struct {
    const char *section;
    uint64_t size;
} as_dwline_end_t;

typedef struct {
    size_t offset;       /* in .debug_line */
    unsigned width;      /* 4 or 8 bytes */
    const char *section; /* the address is this section's start and the addend */
    int64_t addend;
} as_dwline_fixup_t;

typedef struct {
    uint8_t *bytes;
    size_t len;
    as_dwline_fixup_t *fixups;
    size_t fixup_count;
} as_dwline_table_t;

/*
 * The table.  The rows are in the order they were made; those of one
 * section are one sequence, and the sequences are in the order their
 * sections first have a row.  The header is of DWARF 5 if there is a
 * file 0, and of DWARF 3 if not, as GNU as has it.  0, or -1 with the
 * reason.
 */
int as_dwline_build(const as_dwline_file_t *files, size_t file_count, const as_dwline_row_t *rows, size_t row_count,
                    const as_dwline_end_t *ends, size_t end_count, int is_64, as_dwline_table_t *out, char *errbuf,
                    size_t errbuf_sz);

void as_dwline_table_free(as_dwline_table_t *table);

#ifdef __cplusplus
}
#endif

#endif
