/*
 * svr4_tty.h - System V pseudo-terminals and the terminal modules
 * (svr4_tty.c).
 */
#ifndef _EXEC_PERSO_SVR4_SVR4_TTY_H
#define _EXEC_PERSO_SVR4_SVR4_TTY_H

#include <stdint.h>

#include <exec/perso/sysv386.h>

/* ptm's requests, inside I_STR: 'P' << 8 | n (<sys/ptms.h>). */
#define SVR4_ISPTM        0x5001      /* is this a master? */
#define SVR4_UNLKPT       0x5002      /* unlock the slave */

/* The major number a master reports, so that minor(st_rdev) is the
 * slave's number, which is how ptsname() finds /dev/pts/N; and how
 * Release 4 packs the two into st_rdev. */
#define SVR4_PTM_MAJOR    30
#define SVR4_DEV_MINOR_BITS 18

/* ttcompat's requests, 't' << 8 | n (<sys/ttold.h>): the terminal
 * interface of 4.3BSD, which programs written for both still use. */
#define SVR4_TIOCGETD     0x7400
#define SVR4_TIOCSETD     0x7401
#define SVR4_TIOCHPCL     0x7402
#define SVR4_TIOCGETP     0x7408
#define SVR4_TIOCSETP     0x7409
#define SVR4_TIOCSETN     0x740a
#define SVR4_TIOCEXCL     0x740d
#define SVR4_TIOCNXCL     0x740e
#define SVR4_TIOCFLUSH    0x7410
#define SVR4_TIOCSETC     0x7411
#define SVR4_TIOCGETC     0x7412
#define SVR4_TIOCNOTTY    0x7471
#define SVR4_TIOCOUTQ     0x7473
#define SVR4_TIOCGLTC     0x7474
#define SVR4_TIOCSLTC     0x7475
#define SVR4_TIOCCDTR     0x7478
#define SVR4_TIOCSDTR     0x7479
#define SVR4_TIOCCBRK     0x747a
#define SVR4_TIOCSBRK     0x747b
#define SVR4_TIOCLGET     0x747c
#define SVR4_TIOCLSET     0x747d
#define SVR4_TIOCLBIC     0x747e
#define SVR4_TIOCLBIS     0x747f

#define SVR4_NTTYDISC     2           /* TIOCGETD's answer */

/* struct sgttyb's sg_flags. */
#define SVR4_O_TANDEM     0x0001
#define SVR4_O_CBREAK     0x0002
#define SVR4_O_LCASE      0x0004
#define SVR4_O_ECHO       0x0008
#define SVR4_O_CRMOD      0x0010
#define SVR4_O_RAW        0x0020
#define SVR4_O_ODDP       0x0040
#define SVR4_O_EVENP      0x0080
#define SVR4_O_XTABS      0x0c00

/* The local mode word (TIOCLGET and friends). */
#define SVR4_LCRTBS       0x0001
#define SVR4_LPRTERA      0x0002
#define SVR4_LCRTERA      0x0004
#define SVR4_LLITOUT      0x0020
#define SVR4_LTOSTOP      0x0040
#define SVR4_LFLUSHO      0x0080
#define SVR4_LCRTKIL      0x0400
#define SVR4_LPASS8       0x0800
#define SVR4_LCTLECH      0x1000
#define SVR4_LPENDIN      0x2000
#define SVR4_LDECCTQ      0x4000
#define SVR4_LNOFLSH      0x8000

/* Speeds as sgttyb numbers them; all that is reported. */
#define SVR4_B9600        13

struct svr4_sgttyb {
    int8_t  sg_ispeed;
    int8_t  sg_ospeed;
    uint8_t sg_erase;
    uint8_t sg_kill;
    int32_t sg_flags;
};

struct svr4_tchars {
    uint8_t t_intrc, t_quitc, t_startc, t_stopc, t_eofc, t_brkc;
};

struct svr4_ltchars {
    uint8_t t_suspc, t_dsuspc, t_rprntc, t_flushc, t_werasc, t_lnextc;
};

/* ioctl(2): 1 with *result set if the request is one of these on a
 * terminal or a pseudo-terminal's master. */
int svr4_tty_ioctl(struct sysv386_frame *f, int64_t *result);
/* If `fd` is a pseudo-terminal's master, the st_rdev fstat(2) reports for
 * it in *rdev, and 1. */
int svr4_pty_rdev(int fd, uint32_t *rdev);

#endif /* _EXEC_PERSO_SVR4_SVR4_TTY_H */
