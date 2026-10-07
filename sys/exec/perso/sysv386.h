/*
 * sysv386.h - the i386 system call convention shared by Xenix/386 and
 * UNIX System V/386 (perso_sysv386.c).
 *
 * Both enter the kernel with `lcall $7,$0`: the call number in EAX, the
 * arguments on the stack above the stub's return address, carry and an
 * errno in EAX on failure, a second result in EDX, and `lcall $0xf,$0` to
 * return from a signal handler through a trampoline libc supplies.  And
 * both number their first 63 calls the same way, because both descend from
 * the same UNIX: Xenix from System III, with System V's interfaces added
 * to it later, and Release 4 from System V itself.
 *
 * So the entry, the signal frame and those calls are written once, in
 * perso_sysv386.c, and a personality describes what is its own with a
 * struct sysv386_abi: how it numbers signals and open flags, what it calls
 * itself, and the calls it adds or does differently.
 */
#ifndef _EXEC_PERSO_SYSV386_H
#define _EXEC_PERSO_SYSV386_H

#include <stdint.h>

#include <machine/idt.h>
#include <sys/termios.h>

struct sysv386_abi;

/* A decoded call.  nr is AL and sub is AH (the sub-function of a
 * multiplexed call); a[] are the arguments. */
struct sysv386_frame {
    registers_t *regs;
    const struct sysv386_abi *abi;
    uint32_t nr;
    uint32_t sub;
    uint32_t a[6];
};

/*
 * Run the call in `f` and return its result -- a negative errno, or the
 * value for EAX with EDX's in the high half -- or set *known to 0 if there
 * is no such call.
 */
typedef int64_t (*sysv386_callfn)(struct sysv386_frame *f, int *known);

struct sysv386_abi {
    const char *tag;                     /* prefix of the trace lines */
    int (*trace)(void);                  /* tracing is on */
    /* A call's name for the trace, or NULL. */
    const char *(*call_name)(unsigned int nr, unsigned int sub);

    /* The personality's own calls, tried first; what it leaves unknown
     * goes to sysv386_call(). */
    sysv386_callfn call;
    int nosys;                           /* errno for a call nobody has */
    /* Renumber an errno the personality numbers differently, or NULL. */
    int64_t (*fix_errno)(int64_t ret);

    int (*signo)(uint32_t sig);          /* to substrate's, or -EINVAL */
    uint32_t (*signo_from)(int sig);     /* and back */
    unsigned int sig_args;               /* words a handler is entered with */

    int (*open_flags)(uint32_t flags);   /* to substrate's */
    uint32_t (*from_open_flags)(int flags);

    /* read(2) on a directory, where that returns something of the
     * personality's own: non-zero if `fd` is one, with the result in
     * *result.  NULL to read a directory like any file. */
    int (*read_dir)(int fd, uint32_t dst, uint32_t count, int64_t *result);

    /* What utssys(2) reports. */
    const char *sysname, *release, *version, *machine;
};

/* The calls both systems have, by number. */
#define SYSV_SYS_exit     1
#define SYSV_SYS_fork     2
#define SYSV_SYS_read     3
#define SYSV_SYS_write    4
#define SYSV_SYS_open     5
#define SYSV_SYS_close    6
#define SYSV_SYS_wait     7
#define SYSV_SYS_creat    8
#define SYSV_SYS_link     9
#define SYSV_SYS_unlink   10
#define SYSV_SYS_exec     11
#define SYSV_SYS_chdir    12
#define SYSV_SYS_time     13
#define SYSV_SYS_mknod    14
#define SYSV_SYS_chmod    15
#define SYSV_SYS_chown    16
#define SYSV_SYS_brk      17
#define SYSV_SYS_stat     18
#define SYSV_SYS_lseek    19
#define SYSV_SYS_getpid   20
#define SYSV_SYS_setuid   23
#define SYSV_SYS_getuid   24
#define SYSV_SYS_alarm    27
#define SYSV_SYS_fstat    28
#define SYSV_SYS_pause    29
#define SYSV_SYS_access   33
#define SYSV_SYS_nice     34
#define SYSV_SYS_sync     36
#define SYSV_SYS_kill     37
#define SYSV_SYS_setpgrp  39
#define SYSV_SYS_dup      41
#define SYSV_SYS_pipe     42
#define SYSV_SYS_times    43
#define SYSV_SYS_setgid   46
#define SYSV_SYS_getgid   47
#define SYSV_SYS_signal   48
#define SYSV_SYS_ioctl    54
#define SYSV_SYS_utssys   57
#define SYSV_SYS_execve   59
#define SYSV_SYS_umask    60
#define SYSV_SYS_chroot   61
#define SYSV_SYS_fcntl    62
#define SYSV_SYS_ulimit   63
#define SYSV_CALL_MAX     64

/* ioctl(2): the termio requests, 'T' << 8 | n. */
#define SYSV_TCGETA       0x5401
#define SYSV_TCSETA       0x5402
#define SYSV_TCSETAW      0x5403
#define SYSV_TCSETAF      0x5404
#define SYSV_TCSBRK       0x5405
#define SYSV_TCXONC       0x5406
#define SYSV_TCFLSH       0x5407

/*
 * struct termio.  The flag words are 16 bits here and 32 in substrate's
 * struct termios, with the same bits in the low half; c_cc has eight
 * slots, VMIN and VTIME overlaid on VEOF (4) and VEOL (5).  The layout is
 * the same at 16 bits, so the 16-bit Xenix calls use these too.
 */
#define SYSV_NCC 8
struct sysv_termio {
    uint16_t c_iflag;
    uint16_t c_oflag;
    uint16_t c_cflag;
    uint16_t c_lflag;
    char     c_line;
    uint8_t  c_cc[SYSV_NCC];
} __attribute__((packed));

void sysv_termios_to_termio(struct sysv_termio *dst,
                            const struct termios *src);
/* `dst` must hold the terminal's current settings: termio cannot express
 * everything in it, and what it cannot is kept. */
void sysv_termio_to_termios(struct termios *dst,
                            const struct sysv_termio *src);

/*
 * The trap hook: if the #GP/#NP in `regs` was raised by one of the two
 * lcalls, emulate it for `abi` and return 1; otherwise 0.
 */
int sysv386_handle_trap(registers_t *regs, const struct sysv386_abi *abi);

/* The shared calls. */
int64_t sysv386_call(struct sysv386_frame *f, int *known);

/* Enter `handler` for substrate signal `sig`, restoring `mask` on return. */
void sysv386_sendsig(const struct sysv386_abi *abi, void *handler, int sig,
                     uint32_t mask, registers_t *regs);

/* [addr, addr+len) lies below the top of user space: 0, or -EFAULT. */
int sysv386_span(uint32_t addr, uint32_t len);
/*
 * A kernel copy of the path at `addr`; free with sysv386_free_string().
 * For a personality that works in its own tree (struct personality.
 * works_in_tree) an absolute path comes back as the name under the tree,
 * if the file is there or the directory it would be made in is.
 */
int sysv386_string(uint32_t addr, char **out);
/* A kernel copy of the string at `addr` exactly as it is: an argument, an
 * environment string, what a symbolic link is to say.  Freed the same. */
int sysv386_copy_string(uint32_t addr, char **out);
/* Such a path as the program gave it, without the tree's prefix. */
const char *sysv386_given_path(const char *path);
void sysv386_free_string(char *s);
/* stat(2)'s answer for `path` made System V's where it is /dev/fd/N: a
 * character device, not what the descriptor is open on. */
struct stat;
void sysv386_stat_dev_fd(const char *path, struct stat *st);
/* Two results: `first` for EAX, `second` for EDX. */
int64_t sysv386_pair(uint32_t first, uint32_t second);

#endif /* _EXEC_PERSO_SYSV386_H */
