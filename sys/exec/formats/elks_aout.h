#ifndef _ELKS_AOUT_H
#define _ELKS_AOUT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <exec/perso/personality.h>
#include <sys/sysinfo.h>
#include <sys/proc.h>
#include <sys/ldt.h>

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

#define ELKS_LDT_CS_INDEX  0U
#define ELKS_LDT_DS_INDEX  1U
#define ELKS_LDT_SS_INDEX  2U
#define ELKS_LDT_ES_INDEX  3U

struct elks_segment_layout {
    struct user_desc cs;
    struct user_desc ds;
    struct user_desc ss;
    struct user_desc es;
    uint16_t cs_sel;
    uint16_t ds_sel;
    uint16_t ss_sel;
    uint16_t es_sel;
};

static inline uint16_t elks_initial_stack_pointer(const struct elks_load_plan *plan) {
    if (!plan || plan->stack_top == 0) {
        return 0;
    }
    return (uint16_t)(plan->stack_top & ~1U);
}

static inline uint32_t elks_stack_segment_limit(const struct elks_load_plan *plan) {
    if (!plan || plan->data_limit == 0) {
        return 0;
    }
    return (uint32_t)(plan->data_limit - 1U);
}

static inline uint32_t elks_data_segment_limit(const struct elks_load_plan *plan) {
    if (!plan || plan->data_limit == 0) {
        return 0;
    }
    return (uint32_t)(plan->data_limit - 1U);
}

static inline size_t elks_string_vector_count(char *const vec[]) {
    size_t count = 0;

    if (!vec) {
        return 0;
    }
    while (vec[count]) {
        count++;
    }
    return count;
}

static inline size_t elks_string_vector_bytes(char *const vec[]) {
    size_t bytes = 0;
    size_t i;

    if (!vec) {
        return 0;
    }
    for (i = 0; vec[i]; i++) {
        bytes += strlen(vec[i]) + 1U;
    }
    return bytes;
}

static inline size_t elks_stack_image_bytes(char *const argv[], char *const envp[]) {
    size_t argc = elks_string_vector_count(argv);
    size_t envc = elks_string_vector_count(envp);
    size_t ptr_bytes = (1U + argc + 1U + envc + 1U) * sizeof(uint16_t);

    return ptr_bytes + elks_string_vector_bytes(argv) + elks_string_vector_bytes(envp);
}

static inline int elks_build_stack_image(uint8_t *segment,
                                         const struct elks_load_plan *plan,
                                         char *const argv[],
                                         char *const envp[],
                                         uint16_t *initial_sp_out) {
    size_t argc = elks_string_vector_count(argv);
    size_t envc = elks_string_vector_count(envp);
    size_t image_bytes = elks_stack_image_bytes(argv, envp);
    uint16_t sp;
    uint16_t cursor;
    size_t i;
    uint16_t *ptrs;

    if (!segment || !plan) {
        return 0;
    }

    sp = elks_initial_stack_pointer(plan);
    if (sp == 0 || image_bytes > sp) {
        return 0;
    }

    sp = (uint16_t)((sp - image_bytes) & ~1U);
    ptrs = (uint16_t *)(void *)(segment + sp);
    cursor = (uint16_t)(sp + ((1U + argc + 1U + envc + 1U) * sizeof(uint16_t)));

    *ptrs++ = (uint16_t)argc;
    for (i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1U;

        *ptrs++ = cursor;
        memcpy(segment + cursor, argv[i], len);
        cursor = (uint16_t)(cursor + len);
    }
    *ptrs++ = 0;

    for (i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1U;

        *ptrs++ = cursor;
        memcpy(segment + cursor, envp[i], len);
        cursor = (uint16_t)(cursor + len);
    }
    *ptrs++ = 0;

    if (initial_sp_out) {
        *initial_sp_out = sp;
    }
    return 1;
}

static inline void elks_apply_exec_state(process_t *proc,
                                         const struct elks_load_plan *plan,
                                         const char *path) {
    const char *name = path ? path : "";
    const char *p;

    if (!proc || !plan) {
        return;
    }

    proc->perso_id = PERS_ELKS;
    proc->bitness = BITNESS_16;
    proc->brk_start = plan->data_base + plan->brk_offset;
    proc->brk = proc->brk_start;

    for (p = name; *p; p++) {
        if (*p == '/') {
            name = p + 1;
        }
    }

    strncpy(proc->comm, name, sizeof(proc->comm) - 1);
    proc->comm[sizeof(proc->comm) - 1] = '\0';
}

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
    plan->stack_top = (uint16_t)(len & ~1U);
    return 1;
}

static inline void elks_init_data_segment_desc(struct user_desc *desc,
                                               unsigned int entry_number,
                                               uint32_t base,
                                               uint16_t limit) {
    desc->entry_number = entry_number;
    desc->base_addr = base;
    desc->limit = limit ? (uint32_t)(limit - 1U) : 0U;
    desc->seg_32bit = 0;
    desc->contents = 0;
    desc->read_exec_only = 0;
    desc->limit_in_pages = 0;
    desc->seg_not_present = 0;
    desc->useable = 1;
}

static inline void elks_build_segment_layout(const struct elks_load_plan *plan,
                                             struct elks_segment_layout *layout) {
    if (!plan || !layout) {
        return;
    }

    layout->cs.entry_number = ELKS_LDT_CS_INDEX;
    layout->cs.base_addr = plan->text_base;
    layout->cs.limit = plan->text_limit ? (uint32_t)(plan->text_limit - 1U) : 0U;
    layout->cs.seg_32bit = 0;
    layout->cs.contents = 2;
    layout->cs.read_exec_only = 0;
    layout->cs.limit_in_pages = 0;
    layout->cs.seg_not_present = 0;
    layout->cs.useable = 1;

    elks_init_data_segment_desc(&layout->ds, ELKS_LDT_DS_INDEX,
                                plan->data_base, plan->data_limit);
    layout->ds.limit = elks_data_segment_limit(plan);
    elks_init_data_segment_desc(&layout->ss, ELKS_LDT_SS_INDEX,
                                plan->data_base, plan->data_limit);
    layout->ss.limit = elks_stack_segment_limit(plan);
    elks_init_data_segment_desc(&layout->es, ELKS_LDT_ES_INDEX,
                                plan->data_base, plan->data_limit);
    layout->es.limit = elks_data_segment_limit(plan);

    layout->cs_sel = (uint16_t)((ELKS_LDT_CS_INDEX << 3) | 4U | 3U);
    layout->ds_sel = (uint16_t)((ELKS_LDT_DS_INDEX << 3) | 4U | 3U);
    layout->ss_sel = (uint16_t)((ELKS_LDT_SS_INDEX << 3) | 4U | 3U);
    layout->es_sel = (uint16_t)((ELKS_LDT_ES_INDEX << 3) | 4U | 3U);
}

/* Prototypes */
struct exec_binary_handler;
void elks_init_handler(void);
int elks_check_file(const char *path, const char *header, size_t len);
int elks_load(int fd, const char *path, char *const argv[], char *const envp[]);

#endif /* _ELKS_AOUT_H */
