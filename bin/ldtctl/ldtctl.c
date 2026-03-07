/*
 * ldtctl.c - Local Descriptor Table control utility
 *
 * Copyright (c) 2024-2026 The Substrate Project
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>

#if defined(__i386__) || defined(__x86_64__)
#include <sys/ldt.h>
#endif

#ifndef SYS_MODIFY_LDT
#define SYS_MODIFY_LDT 123
#endif

#ifndef LDT_ENTRIES
#define LDT_ENTRIES 8192
#define LDT_ENTRY_SIZE 8
#endif

#ifndef LDT_READ
#define LDT_READ 0
#define LDT_WRITE 1
#endif

/* Fallback definition for host compilation if <sys/ldt.h> is unavailable */
#ifndef _SYS_LDT_H
#define _SYS_LDT_H
struct user_desc {
    unsigned int  entry_number;
    unsigned int  base_addr;
    unsigned int  limit;
    unsigned int  seg_32bit:1;
    unsigned int  contents:2;
    unsigned int  read_exec_only:1;
    unsigned int  limit_in_pages:1;
    unsigned int  seg_not_present:1;
    unsigned int  useable:1;
#ifdef __x86_64__
    unsigned int  lm:1;
#endif
};
#endif

static int do_modify_ldt(int func, void *ptr, unsigned long bytecount) {
    return syscall(SYS_MODIFY_LDT, func, (uintptr_t)ptr, bytecount, 0, 0, 0, 0);
}

static void usage(const char *prog) {
    const char *base = strrchr(prog, '/');
    base = base ? base + 1 : prog;
    
    fprintf(stderr, "usage: %s read [entry]\n", base);
    fprintf(stderr, "       %s write <entry> <base> <limit> [flags...]\n", base);
    fprintf(stderr, "       %s clear <entry>\n", base);
    fprintf(stderr, "\nFlags for write:\n");
    fprintf(stderr, "  --32           32-bit segment (default: 16-bit)\n");
    fprintf(stderr, "  --contents=N   Contents type (0=data, 1=stack, 2=code) (default: 0)\n");
    fprintf(stderr, "  --rx           Read/Execute only (default: Read/Write for data, Exec/Read for code)\n");
    fprintf(stderr, "  --pages        Limit is in 4KB pages (default: bytes)\n");
    fprintf(stderr, "  --not-present  Segment is not present (default: present)\n");
    fprintf(stderr, "  --useable      Available for use by system software (AVL bit) (default: 0)\n");
#ifdef __x86_64__
    fprintf(stderr, "  --lm           Long mode (64-bit) (default: 0)\n");
#endif
    exit(1);
}

static void print_entry(int index, uint8_t *desc) {
    uint32_t a = *(uint32_t*)desc;
    uint32_t b = *(uint32_t*)(desc + 4);
    
    if (a == 0 && b == 0) return; /* Skip empty descriptor */
    
    uint32_t base = (a >> 16) | ((b & 0xff) << 16) | (b & 0xff000000);
    uint32_t limit = (a & 0xffff) | (b & 0xf0000);
    int type = (b >> 8) & 0x1f;
    int dpl = (b >> 13) & 3;
    int p = (b >> 15) & 1;
    int avl = (b >> 20) & 1;
    int l = (b >> 21) & 1;
    int d_b = (b >> 22) & 1;
    int g = (b >> 23) & 1;
    
    if (g) limit = (limit << 12) | 0xfff;
    
    printf("Entry %4d: Base=0x%08x Limit=0x%08x\n", index, base, limit);
    printf("            Type=0x%02x DPL=%d P=%d AVL=%d L=%d D/B=%d G=%d\n", type, dpl, p, avl, l, d_b, g);
}

static int do_read(int argc, char *argv[]) {
    int target_entry = -1;
    if (argc > 2) {
        target_entry = atoi(argv[2]);
        if (target_entry < 0 || target_entry >= LDT_ENTRIES) {
            fprintf(stderr, "Error: entry number out of bounds (0-%d)\n", LDT_ENTRIES-1);
            return 1;
        }
    }
    
    size_t size = LDT_ENTRIES * LDT_ENTRY_SIZE;
    uint8_t *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        return 1;
    }
    memset(buf, 0, size);
    
    int ret = do_modify_ldt(LDT_READ, buf, size);
    if (ret < 0) {
        perror("modify_ldt(READ)");
        free(buf);
        return 1;
    }
    
    int num_entries = ret / LDT_ENTRY_SIZE;
    if (target_entry >= 0) {
        if (target_entry >= num_entries) {
            printf("Entry %d is empty.\n", target_entry);
        } else {
            print_entry(target_entry, buf + target_entry * LDT_ENTRY_SIZE);
        }
    } else {
        for (int i = 0; i < num_entries; i++) {
            print_entry(i, buf + i * LDT_ENTRY_SIZE);
        }
    }
    
    free(buf);
    return 0;
}

static int do_write(int argc, char *argv[]) {
    if (argc < 5) usage(argv[0]);
    
    struct user_desc desc;
    memset(&desc, 0, sizeof(desc));
    
    desc.entry_number = strtoul(argv[2], NULL, 0);
    desc.base_addr = strtoul(argv[3], NULL, 0);
    desc.limit = strtoul(argv[4], NULL, 0);
    
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--32") == 0) {
            desc.seg_32bit = 1;
        } else if (strncmp(argv[i], "--contents=", 11) == 0) {
            desc.contents = atoi(argv[i] + 11);
        } else if (strcmp(argv[i], "--rx") == 0) {
            desc.read_exec_only = 1;
        } else if (strcmp(argv[i], "--pages") == 0) {
            desc.limit_in_pages = 1;
        } else if (strcmp(argv[i], "--not-present") == 0) {
            desc.seg_not_present = 1;
        } else if (strcmp(argv[i], "--useable") == 0) {
            desc.useable = 1;
        } else if (strcmp(argv[i], "--lm") == 0) {
#ifdef __x86_64__
            desc.lm = 1;
#else
            fprintf(stderr, "Warning: --lm ignored on 32-bit build\n");
#endif
        } else {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            return 1;
        }
    }
    
    int ret = do_modify_ldt(LDT_WRITE, &desc, sizeof(desc));
    if (ret < 0) {
        perror("modify_ldt(WRITE)");
        return 1;
    }
    
    return 0;
}

static int do_clear(int argc, char *argv[]) {
    if (argc < 3) usage(argv[0]);
    
    struct user_desc desc;
    memset(&desc, 0, sizeof(desc));
    
    desc.entry_number = strtoul(argv[2], NULL, 0);
    desc.read_exec_only = 1;
    desc.seg_not_present = 1;
    
    int ret = do_modify_ldt(LDT_WRITE, &desc, sizeof(desc));
    if (ret < 0) {
        perror("modify_ldt(CLEAR)");
        return 1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) usage(argv[0]);
    
    if (strcmp(argv[1], "read") == 0) {
        return do_read(argc, argv);
    } else if (strcmp(argv[1], "write") == 0) {
        return do_write(argc, argv);
    } else if (strcmp(argv[1], "clear") == 0) {
        return do_clear(argc, argv);
    } else {
        usage(argv[0]);
    }
    
    return 1;
}
