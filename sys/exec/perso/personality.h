#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

#include <stdint.h>

#ifndef MAX_SYSCALLS
#define MAX_SYSCALLS 600
#endif

// Argument Types for Tracing
#define ARG_HEX  0
#define ARG_INT  1
#define ARG_STR  2
#define ARG_PTR  3
#define ARG_LONG 4  /* 64-bit value (consumes 2 slots on i386) */

struct syscall_fmt {
    int nargs;
    int arg_types[6];
};

enum personality_type {
    PERS_NATIVE  = 0,
    PERS_LINUX   = 3,
    PERS_SVR4    = 4,
    PERS_SVR3    = 5,
    PERS_SOLARIS = 6,
    PERS_FREEBSD = 9,
    PERS_NETBSD  = 2,
    PERS_OPENBSD = 12,
    /* Values >= 128 reserved for non-ELF personalities */
    PERS_SUNOS   = 129,
    PERS_ELKS    = 130,
    /*
     * Xenix: one personality for every x.out program (perso_xenix.c).
     *
     * It has two system-call ABIs -- the 8086 and 80286 images are 16-bit
     * segmented and trap through `int $5` with register arguments, the
     * 80386 ones are 32-bit and use the SysV `lcall $7,$0` gate -- and
     * they used to be a personality each, with more ids reserved for the
     * 8086 and the Microsoft-branded releases.  But they are one system:
     * a Xenix/386 installation holds all three kinds of binary in one
     * tree and a program of one kind execs another, and every 8086 and
     * 80286 binary tried, SCO's and IBM's, runs under the same 16-bit
     * half.  The process's bitness says which half applies.
     * (132-136 were the ids that split it; they are free.)
     */
    PERS_XENIX     = 131,
    /* PC/IX: System III for the IBM PC, in perso_xenix.c beside the
     * 16-bit Xenix it shares its calls with. */
    PERS_PCIX      = 132,
    /* Venix/86: Version 7 for the IBM PC, in the same file for the same
     * reason. */
    PERS_VENIX     = 133,
    PERS_MAX     = 256
};

struct personality {
    const char *name;
    enum personality_type id;
    void **syscall_table;
    const char **syscall_names;
    struct syscall_fmt *syscall_fmts;
    uint32_t syscall_count;

    /* Filesystem prefix under which this personality's binaries/libs live.
     * The ELF loader prepends this to PT_INTERP paths and library lookups.
     * NULL means use the root filesystem directly (native personality). */
    const char *path_prefix;
    /* Non-zero if /dev is the kernel's for this personality whatever its
     * tree holds there: the prefix is not tried for /dev or anything
     * below it.  A tree built from distribution media has a /dev of empty
     * directories, which would otherwise be what a program lists when it
     * looks for its terminal's name. */
    int native_dev;
    /* Non-zero if the tree under path_prefix is where this personality's
     * programs work, not only where their files are found: chdir(2) to an
     * absolute path goes into the tree if the directory is there, and so
     * does the directory part of a name being created, removed, linked or
     * renamed.  Without it only a file that already exists is found under
     * the prefix, so a program can read from a directory the tree alone
     * has and not write to it. */
    int works_in_tree;

    /* Signal hooks */
    void (*sendsig)(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
    int (*sigreturn)(void *regs);
    int (*rt_sigreturn)(void *regs);

    /* Optional user-exception hook. Return nonzero when fully handled. */
    int (*handle_trap)(void *regs);
};

extern struct personality personality_native;
extern struct personality personality_freebsd;
extern struct personality personality_linux;
extern struct personality personality_svr3;
extern struct personality personality_svr4;
extern struct personality personality_netbsd;
extern struct personality personality_openbsd;
extern struct personality personality_solaris;
extern struct personality personality_sunos;
extern struct personality personality_elks;
extern struct personality personality_xenix;
extern struct personality personality_pcix;
extern struct personality personality_venix;

void elks_personality_init(void);

struct personality *perso_lookup(int id);
const char *perso_name(int id);
/* The name the current process's personality means by the absolute
 * `path`, if it works in its own tree and the name belongs there: 1 with
 * it in `out`, or 0 to use `path` as given (personality.c). */
int perso_tree_path(const char *path, char *out, size_t size);

/*
 * A socket address between the current process's form and the kernel's
 * (personality.c): `_in` on one copied in from the process, `_out` on one
 * about to be copied out to it, of `len` bytes.  Nothing is done for a
 * personality whose struct sockaddr is the kernel's.
 */
void perso_sockaddr_in(uint8_t *addr, size_t len);
void perso_sockaddr_out(uint8_t *addr, size_t len);
/* The level of a socket option or control message, likewise: the process's
 * to the kernel's, or the kernel's to the process's when `to_user`. */
int perso_socket_level(int level, int to_user);
/* 4.4BSD's numbers where they are not substrate's. */
#define PERSO_BSD_SOL_SOCKET 0xffff
#define PERSO_BSD_AF_INET6   28

#endif
