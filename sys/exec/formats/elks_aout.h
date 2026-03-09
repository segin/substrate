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

#define ELKS_TEXT_BASE           0x00010000UL
#define ELKS_FARTEXT_BASE        0x00020000UL
#define ELKS_DATA_BASE           0x00030000UL

#define ELKS_COMBID               0x04100301UL
#define ELKS_SPLITID              0x04200301UL
#define ELKS_SPLITID_AHISTORICAL  0x04300301UL

#define ELKS_INIT_HEAP           4096U
#define ELKS_INIT_STACK          4096U
#define ELKS_MINIX_HDR_SIZE  ((uint8_t)sizeof(struct elks_exec))
#define ELKS_RELOC_HDR_SIZE  ((uint8_t)(sizeof(struct elks_exec) + 16))
#define ELKS_FARTEXT_HDR_SIZE ((uint8_t)sizeof(struct elks_exec) + (uint8_t)sizeof(struct elks_supl_hdr))

struct elks_load_plan {
    uint32_t text_base;
    uint32_t data_base;
    uint32_t fartext_base;
    uint32_t text_file_offset;
    uint32_t fartext_file_offset;
    uint32_t data_file_offset;
    uint16_t text_size;
    uint16_t data_size;
    uint16_t bss_size;
    uint16_t fartext_size;
    uint16_t text_limit;
    uint16_t data_limit;
    uint16_t brk_offset;
    uint16_t stack_top;
    uint8_t combined;
};

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

static inline int elks_supl_header_valid(const struct elks_supl_hdr *suph) {
    if (!suph) {
        return 1;
    }
    if (suph->msh_tbase != 0 || suph->msh_dbase != 0) {
        return 0;
    }
    if ((suph->msh_trsize % 8) != 0 ||
        (suph->msh_drsize % 8) != 0 ||
        (suph->esh_ftrsize % 8) != 0) {
        return 0;
    }
    return 1;
}

static inline int elks_build_load_plan(const struct elks_exec *hdr,
                                       const struct elks_supl_hdr *suph,
                                       uint16_t argv_envp_bytes,
                                       struct elks_load_plan *plan) {
    size_t min_len;
    size_t len;
    size_t heap;
    size_t stack;
    struct elks_supl_hdr zero_suph;

    if (!hdr || !plan) {
        return 0;
    }
    if (!elks_header_type_valid(hdr->type) ||
        !elks_header_hlen_valid(hdr->hlen) ||
        hdr->tseg == 0 ||
        (hdr->version != 0 && hdr->version != 1)) {
        return 0;
    }
    if (!suph) {
        zero_suph.msh_trsize = 0;
        zero_suph.msh_drsize = 0;
        zero_suph.msh_tbase = 0;
        zero_suph.msh_dbase = 0;
        zero_suph.esh_ftseg = 0;
        zero_suph.esh_reserved1 = 0;
        zero_suph.esh_ftrsize = 0;
        zero_suph.esh_reserved2 = 0;
        zero_suph.esh_reserved3 = 0;
        suph = &zero_suph;
    }
    if (!elks_supl_header_valid(suph)) {
        return 0;
    }

    min_len = (size_t)hdr->dseg + (size_t)hdr->bseg;
    if (min_len > 0xFFFFU) {
        return 0;
    }

    switch (hdr->version) {
    case 0:
        stack = ELKS_INIT_STACK;
        len = hdr->chmem;
        if (len != 0) {
            if (len <= min_len) {
                return 0;
            }
            heap = len - min_len;
            if (heap < stack || (heap - stack) < argv_envp_bytes) {
                return 0;
            }
        } else {
            len = min_len;
            if (hdr->type == ELKS_COMBID) {
                len += hdr->tseg;
            }
            len += ELKS_INIT_HEAP + stack + argv_envp_bytes;
            if (len > 0xFFFFU) {
                return 0;
            }
        }
        break;
    case 1:
        len = min_len;
        stack = hdr->minstack ? hdr->minstack : ELKS_INIT_STACK;
        len += stack + argv_envp_bytes;
        if (len > 0xFFFFU) {
            return 0;
        }
        heap = hdr->chmem ? hdr->chmem : ELKS_INIT_HEAP;
        if (heap >= 0xFFF0U) {
            if (len < 0xFFF0U) {
                len = 0xFFF0U;
            }
        } else {
            len += heap;
            if (len > 0xFFFFU) {
                return 0;
            }
        }
        break;
    default:
        return 0;
    }

    plan->combined = (uint8_t)(hdr->type == ELKS_COMBID);
    plan->text_base = ELKS_TEXT_BASE;
    plan->fartext_base = ELKS_FARTEXT_BASE;
    plan->data_base = plan->combined ? ELKS_TEXT_BASE : ELKS_DATA_BASE;
    plan->text_file_offset = hdr->hlen;
    plan->fartext_file_offset = hdr->hlen + hdr->tseg;
    plan->data_file_offset = hdr->hlen + hdr->tseg + suph->esh_ftseg;
    plan->text_size = hdr->tseg;
    plan->data_size = hdr->dseg;
    plan->bss_size = hdr->bseg;
    plan->fartext_size = suph->esh_ftseg;
    plan->text_limit = plan->combined ? (uint16_t)len : hdr->tseg;
    plan->data_limit = (uint16_t)len;
    plan->brk_offset = (uint16_t)(hdr->dseg + hdr->bseg);
    plan->stack_top = (uint16_t)len;
    return 1;
}

/* Prototypes */
struct exec_binary_handler;
void elks_init_handler(void);
int elks_check_file(const char *path, const char *header, size_t len);
int elks_load(int fd, const char *path, char *const argv[], char *const envp[]);

#endif /* _ELKS_AOUT_H */
