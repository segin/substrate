#ifndef _ELKS_AOUT_H
#define _ELKS_AOUT_H

#include <stdint.h>
#include <stddef.h>

/*
 * ELKS uses the Minix-style 16-bit a.out header layout consumed by elksemu.
 * The first four bytes are a little-endian type value, not a standalone
 * 16-bit magic/CPU pair.
 */
struct __attribute__((packed)) elks_exec {
    uint32_t type;
    uint8_t  hlen;
    uint8_t  reserved1;
    uint16_t version;
    uint16_t tseg;
    uint16_t reserved2;
    uint16_t dseg;
    uint16_t reserved3;
    uint16_t bseg;
    uint16_t reserved4;
    uint32_t entry;
    uint16_t chmem;
    uint16_t minstack;
    uint32_t syms;
};

struct __attribute__((packed)) elks_supl_hdr {
    uint32_t msh_trsize;
    uint32_t msh_drsize;
    uint32_t msh_tbase;
    uint32_t msh_dbase;
    uint16_t esh_ftseg;
    uint16_t esh_reserved1;
    uint32_t esh_ftrsize;
    uint32_t esh_reserved2;
    uint32_t esh_reserved3;
};

#define ELKS_COMBID               0x04100301UL
#define ELKS_SPLITID              0x04200301UL
#define ELKS_SPLITID_AHISTORICAL  0x04300301UL

#define ELKS_MINIX_HDR_SIZE  ((uint8_t)sizeof(struct elks_exec))
#define ELKS_RELOC_HDR_SIZE  ((uint8_t)(sizeof(struct elks_exec) + 16))
#define ELKS_FARTEXT_HDR_SIZE ((uint8_t)sizeof(struct elks_exec) + (uint8_t)sizeof(struct elks_supl_hdr))

static inline int elks_header_type_valid(uint32_t type) {
    return type == ELKS_COMBID ||
           type == ELKS_SPLITID ||
           type == ELKS_SPLITID_AHISTORICAL;
}

static inline int elks_header_hlen_valid(uint8_t hlen) {
    return hlen == ELKS_MINIX_HDR_SIZE ||
           hlen == ELKS_RELOC_HDR_SIZE ||
           hlen == ELKS_FARTEXT_HDR_SIZE;
}

static inline int elks_header_recognized(const void *header, size_t len) {
    const struct elks_exec *hdr = (const struct elks_exec *)header;

    if (!header || len < sizeof(struct elks_exec)) {
        return 0;
    }
    if (!elks_header_type_valid(hdr->type)) {
        return 0;
    }
    if (!elks_header_hlen_valid(hdr->hlen)) {
        return 0;
    }
    if (hdr->hlen > len) {
        return 0;
    }
    if (hdr->tseg == 0) {
        return 0;
    }
    if (hdr->version != 0 && hdr->version != 1) {
        return 0;
    }

    return 1;
}

/* Prototypes */
struct exec_binary_handler;
void elks_init_handler(void);
int elks_check_file(const char *path, const char *header, size_t len);
int elks_load(int fd, const char *path, char *const argv[], char *const envp[]);

#endif /* _ELKS_AOUT_H */
