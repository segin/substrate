/*
 * svr4_tty.c - System V pseudo-terminals, and the modules of a terminal
 * stream.
 *
 * A pseudo-terminal in System V is a pair of streams.  A program opens the
 * clone device /dev/ptmx for the master; ptsname() asks the master whether
 * it is one (I_STR, ISPTM) and makes the slave's name, /dev/pts/N, from the
 * minor number fstat() reports for it; grantpt() runs a set-id helper that
 * does the same and gives the slave to the user; unlockpt() is I_STR,
 * UNLKPT.  Whoever opens the slave then builds a terminal out of it by
 * pushing modules: `ptem`, the terminal emulation, `ldterm`, the line
 * discipline, and, for programs that speak the 4.3BSD terminal interface,
 * `ttcompat`.
 *
 * Substrate's own pseudo-terminals are /dev/ptmx and /dev/pts/N already
 * (drivers/console/pty.c), with the line discipline in place on the slave
 * from the start.  So what is here is the System V way of asking: the two
 * ptm requests, the master's device number, pushes of the three modules
 * onto something that is a terminal without them, and ttcompat's requests
 * -- sgttyb, tchars, ltchars and the local mode word -- carried out on the
 * terminal's termios.  The last are wanted on any terminal, a BSD-named
 * pseudo-terminal (/dev/ptyp0, /dev/ttyp0) included, which is what the
 * xterm of both Dell UNIX and INTERACTIVE UNIX takes first.
 *
 * Layouts and numbers are those of <sys/ptms.h>, <sys/ttold.h> and
 * <sys/ttcompat.h> in UNIX System V/386 Release 4.0.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <drivers/console/pty.h>
#include <exec/perso/svr4/svr4_streams.h>
#include <exec/perso/svr4/svr4_tty.h>
#include <exec/perso/sysv386.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/termios.h>
#include <vfs/vfs.h>

static int user_put(uint32_t dst, const void *src, uint32_t len) {
    if (sysv386_span(dst, len) != 0 ||
        copyout(src, (void *)(uintptr_t)dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int user_get(uint32_t src, void *dst, uint32_t len) {
    if (sysv386_span(src, len) != 0 ||
        copyin((const void *)(uintptr_t)src, dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

static fs_node_t *fd_node(int fd) {
    file_t *fp;

    if (!current_process || fd < 0 || fd >= MAX_FD) {
        return NULL;
    }
    fp = current_process->fds[fd];
    return fp ? (fs_node_t *)fp->f_data : NULL;
}

int svr4_pty_rdev(int fd, uint32_t *rdev) {
    int index = pty_master_index(fd_node(fd));

    if (index < 0) {
        return 0;
    }
    *rdev = ((uint32_t)SVR4_PTM_MAJOR << SVR4_DEV_MINOR_BITS) |
            (uint32_t)index;
    return 1;
}

/* ---- ttcompat -------------------------------------------------------- */

static void sgttyb_from_termios(struct svr4_sgttyb *sg,
                                const struct termios *t) {
    memset(sg, 0, sizeof(*sg));
    sg->sg_ispeed = SVR4_B9600;
    sg->sg_ospeed = SVR4_B9600;
    sg->sg_erase = t->c_cc[VERASE];
    sg->sg_kill = t->c_cc[VKILL];
    if (t->c_lflag & ECHO)  sg->sg_flags |= SVR4_O_ECHO;
    if (t->c_iflag & ICRNL) sg->sg_flags |= SVR4_O_CRMOD;
    if (t->c_iflag & IXOFF) sg->sg_flags |= SVR4_O_TANDEM;
    if (t->c_iflag & IUCLC) sg->sg_flags |= SVR4_O_LCASE;
    if (!(t->c_lflag & ICANON)) {
        /* Raw gives up signals and output processing too; cbreak keeps
         * them. */
        sg->sg_flags |= (t->c_lflag & ISIG) ? SVR4_O_CBREAK : SVR4_O_RAW;
    }
    if (!(t->c_cflag & PARENB)) {
        sg->sg_flags |= SVR4_O_ODDP | SVR4_O_EVENP;     /* any parity */
    } else {
        sg->sg_flags |= (t->c_cflag & PARODD) ? SVR4_O_ODDP : SVR4_O_EVENP;
    }
}

static void sgttyb_to_termios(struct termios *t,
                              const struct svr4_sgttyb *sg) {
    int32_t fl = sg->sg_flags;

    t->c_cc[VERASE] = sg->sg_erase;
    t->c_cc[VKILL] = sg->sg_kill;
    t->c_lflag = (fl & SVR4_O_ECHO) ? (t->c_lflag | ECHO)
                                    : (t->c_lflag & ~(tcflag_t)ECHO);
    if (fl & SVR4_O_CRMOD) {
        t->c_iflag |= ICRNL;
        t->c_oflag |= ONLCR;
    } else {
        t->c_iflag &= ~(tcflag_t)ICRNL;
        t->c_oflag &= ~(tcflag_t)ONLCR;
    }
    t->c_iflag = (fl & SVR4_O_TANDEM) ? (t->c_iflag | IXOFF)
                                      : (t->c_iflag & ~(tcflag_t)IXOFF);
    if (fl & SVR4_O_LCASE) {
        t->c_iflag |= IUCLC;
        t->c_oflag |= OLCUC;
    } else {
        t->c_iflag &= ~(tcflag_t)IUCLC;
        t->c_oflag &= ~(tcflag_t)OLCUC;
    }
    if (fl & SVR4_O_RAW) {
        t->c_lflag &= ~(tcflag_t)(ICANON | ISIG | IEXTEN);
        t->c_iflag &= ~(tcflag_t)(ICRNL | IXON | ISTRIP | BRKINT | INPCK);
        t->c_oflag &= ~(tcflag_t)OPOST;
        t->c_cflag = (t->c_cflag & ~(tcflag_t)(CSIZE | PARENB)) | CS8;
        t->c_cc[VMIN] = 1;
        t->c_cc[VTIME] = 0;
        return;
    }
    t->c_lflag |= ISIG;
    t->c_oflag |= OPOST;
    if (fl & SVR4_O_CBREAK) {
        t->c_lflag &= ~(tcflag_t)ICANON;
        t->c_cc[VMIN] = 1;
        t->c_cc[VTIME] = 0;
    } else {
        t->c_lflag |= ICANON;
    }
    switch (fl & (SVR4_O_ODDP | SVR4_O_EVENP)) {
    case SVR4_O_ODDP:
        t->c_cflag = (t->c_cflag & ~(tcflag_t)CSIZE) | CS7 | PARENB | PARODD;
        break;
    case SVR4_O_EVENP:
        t->c_cflag = ((t->c_cflag & ~(tcflag_t)(CSIZE | PARODD)) | CS7 |
                      PARENB);
        break;
    default:
        t->c_cflag = (t->c_cflag & ~(tcflag_t)(CSIZE | PARENB)) | CS8;
        break;
    }
}

static int32_t local_from_termios(const struct termios *t) {
    int32_t l = 0;

    if (t->c_lflag & ECHOE)   l |= SVR4_LCRTBS | SVR4_LCRTERA;
    if (t->c_lflag & ECHOPRT) l |= SVR4_LPRTERA;
    if (t->c_lflag & ECHOKE)  l |= SVR4_LCRTKIL;
    if (t->c_lflag & ECHOCTL) l |= SVR4_LCTLECH;
    if (t->c_lflag & TOSTOP)  l |= SVR4_LTOSTOP;
    if (t->c_lflag & FLUSHO)  l |= SVR4_LFLUSHO;
    if (t->c_lflag & PENDIN)  l |= SVR4_LPENDIN;
    if (t->c_lflag & NOFLSH)  l |= SVR4_LNOFLSH;
    if (!(t->c_oflag & OPOST)) l |= SVR4_LLITOUT;
    if (!(t->c_iflag & IXANY)) l |= SVR4_LDECCTQ;
    if (!(t->c_iflag & ISTRIP) && (t->c_cflag & CSIZE) == CS8) {
        l |= SVR4_LPASS8;
    }
    return l;
}

static void flag(tcflag_t *word, tcflag_t bit, int on) {
    *word = on ? (*word | bit) : (*word & ~bit);
}

static void local_to_termios(struct termios *t, int32_t l) {
    flag(&t->c_lflag, ECHOE, l & (SVR4_LCRTBS | SVR4_LCRTERA));
    flag(&t->c_lflag, ECHOPRT, l & SVR4_LPRTERA);
    flag(&t->c_lflag, ECHOKE, l & SVR4_LCRTKIL);
    flag(&t->c_lflag, ECHOCTL, l & SVR4_LCTLECH);
    flag(&t->c_lflag, TOSTOP, l & SVR4_LTOSTOP);
    flag(&t->c_lflag, FLUSHO, l & SVR4_LFLUSHO);
    flag(&t->c_lflag, PENDIN, l & SVR4_LPENDIN);
    flag(&t->c_lflag, NOFLSH, l & SVR4_LNOFLSH);
    flag(&t->c_oflag, OPOST, !(l & SVR4_LLITOUT));
    flag(&t->c_iflag, IXANY, !(l & SVR4_LDECCTQ));
    if (l & SVR4_LPASS8) {
        t->c_iflag &= ~(tcflag_t)ISTRIP;
        t->c_cflag = (t->c_cflag & ~(tcflag_t)(CSIZE | PARENB)) | CS8;
    }
}

/* A request of ttcompat's on the terminal `fd`, whose settings are `t`:
 * 1 with *result set, or 0 if it is not one. */
static int ttcompat_ioctl(int fd, uint32_t request, uint32_t arg,
                          struct termios *t, int64_t *result) {
    struct svr4_sgttyb sg;
    struct svr4_tchars tc;
    struct svr4_ltchars lt;
    int32_t word;
    int rc = 0;

    switch (request) {
    case SVR4_TIOCGETP:
        sgttyb_from_termios(&sg, t);
        *result = user_put(arg, &sg, sizeof(sg));
        return 1;
    case SVR4_TIOCSETP:
    case SVR4_TIOCSETN:
        rc = user_get(arg, &sg, sizeof(sg));
        if (rc == 0) {
            sgttyb_to_termios(t, &sg);
            /* TIOCSETP waits for output and throws input away. */
            rc = kern_ioctl(fd, request == SVR4_TIOCSETP ? TCSETSF : TCSETS,
                            t);
        }
        *result = rc;
        return 1;
    case SVR4_TIOCGETC:
        tc.t_intrc = t->c_cc[VINTR];
        tc.t_quitc = t->c_cc[VQUIT];
        tc.t_startc = t->c_cc[VSTART];
        tc.t_stopc = t->c_cc[VSTOP];
        tc.t_eofc = t->c_cc[VEOF];
        tc.t_brkc = t->c_cc[VEOL];
        *result = user_put(arg, &tc, sizeof(tc));
        return 1;
    case SVR4_TIOCSETC:
        rc = user_get(arg, &tc, sizeof(tc));
        if (rc == 0) {
            t->c_cc[VINTR] = tc.t_intrc;
            t->c_cc[VQUIT] = tc.t_quitc;
            t->c_cc[VSTART] = tc.t_startc;
            t->c_cc[VSTOP] = tc.t_stopc;
            t->c_cc[VEOF] = tc.t_eofc;
            t->c_cc[VEOL] = tc.t_brkc;
            rc = kern_ioctl(fd, TCSETS, t);
        }
        *result = rc;
        return 1;
    case SVR4_TIOCGLTC:
        lt.t_suspc = t->c_cc[VSUSP];
        lt.t_dsuspc = 0;                /* no delayed suspend */
        lt.t_rprntc = t->c_cc[VREPRINT];
        lt.t_flushc = t->c_cc[VDISCARD];
        lt.t_werasc = t->c_cc[VWERASE];
        lt.t_lnextc = t->c_cc[VLNEXT];
        *result = user_put(arg, &lt, sizeof(lt));
        return 1;
    case SVR4_TIOCSLTC:
        rc = user_get(arg, &lt, sizeof(lt));
        if (rc == 0) {
            t->c_cc[VSUSP] = lt.t_suspc;
            t->c_cc[VREPRINT] = lt.t_rprntc;
            t->c_cc[VDISCARD] = lt.t_flushc;
            t->c_cc[VWERASE] = lt.t_werasc;
            t->c_cc[VLNEXT] = lt.t_lnextc;
            rc = kern_ioctl(fd, TCSETS, t);
        }
        *result = rc;
        return 1;
    case SVR4_TIOCLGET:
        word = local_from_termios(t);
        *result = user_put(arg, &word, sizeof(word));
        return 1;
    case SVR4_TIOCLSET:
    case SVR4_TIOCLBIS:
    case SVR4_TIOCLBIC:
        rc = user_get(arg, &word, sizeof(word));
        if (rc == 0) {
            int32_t now = local_from_termios(t);

            word = request == SVR4_TIOCLSET ? word
                 : request == SVR4_TIOCLBIS ? (now | word) : (now & ~word);
            local_to_termios(t, word);
            rc = kern_ioctl(fd, TCSETS, t);
        }
        *result = rc;
        return 1;
    case SVR4_TIOCGETD:
        word = SVR4_NTTYDISC;
        *result = user_put(arg, &word, sizeof(word));
        return 1;
    case SVR4_TIOCOUTQ:
        word = 0;
        *result = user_put(arg, &word, sizeof(word));
        return 1;
    case SVR4_TIOCFLUSH:
        *result = kern_ioctl(fd, TCFLSH, (void *)(uintptr_t)TCIOFLUSH);
        return 1;
    case SVR4_TIOCNOTTY:
        *result = kern_ioctl(fd, TIOCNOTTY, NULL);
        return 1;
    case SVR4_TIOCSETD:                 /* there is one line discipline */
    case SVR4_TIOCHPCL:
    case SVR4_TIOCEXCL:
    case SVR4_TIOCNXCL:
    case SVR4_TIOCSBRK:
    case SVR4_TIOCCBRK:
    case SVR4_TIOCSDTR:
    case SVR4_TIOCCDTR:
        *result = 0;
        return 1;
    default:
        return 0;
    }
}

/* ---- the modules, and ptm -------------------------------------------- */

static int is_tty_module(const char *name) {
    return strcmp(name, "ptem") == 0 || strcmp(name, "ldterm") == 0 ||
           strcmp(name, "ttcompat") == 0;
}

static int module_name(uint32_t arg, char *name) {
    uint32_t i;

    memset(name, 0, SVR4_FMNAMESZ + 1);
    for (i = 0; i < SVR4_FMNAMESZ; i++) {
        if (user_get(arg + i, &name[i], 1) != 0) {
            return -EFAULT;
        }
        if (name[i] == '\0') {
            break;
        }
    }
    return 0;
}

int svr4_tty_ioctl(struct sysv386_frame *f, int64_t *result) {
    int fd = (int)f->a[0];
    uint32_t request = f->a[1], arg = f->a[2];
    fs_node_t *node = fd_node(fd);
    char name[SVR4_FMNAMESZ + 1];
    struct svr4_strioctl ioc;
    struct termios t;
    int is_master, is_tty, rc;

    if (!node) {
        return 0;
    }
    /* None of these is asked often enough for the question to cost. */
    switch (request) {
    case SVR4_I_PUSH: case SVR4_I_POP: case SVR4_I_LOOK: case SVR4_I_FIND:
    case SVR4_I_STR:
        break;
    default:
        if ((request & 0xff00U) != 0x7400U) {
            return 0;
        }
        break;
    }
    is_master = pty_master_index(node) >= 0;
    is_tty = kern_ioctl(fd, TCGETS, &t) == 0;
    if (!is_master && !is_tty) {
        return 0;
    }

    switch (request) {
    case SVR4_I_STR:
        if (!is_master) {
            return 0;
        }
        rc = user_get(arg, &ioc, sizeof(ioc));
        if (rc != 0) {
            *result = rc;
            return 1;
        }
        if (ioc.ic_cmd == SVR4_ISPTM) {
            /*
             * ptsname() is asking, and whoever asks goes on to use the
             * name: grantpt()'s helper calls stat(2) and chown(2) on the
             * slave before unlockpt() is ever called.  In System V the
             * slave's node is always there; substrate makes it when the
             * slave is unlocked.  So it is unlocked here.  It belongs to
             * whoever opened the master and to nobody else from the
             * start, so nothing can open it that could not have anyway.
             */
            *result = pty_master_unlock(node);
        } else if (ioc.ic_cmd == SVR4_UNLKPT) {
            *result = pty_master_unlock(node);
        } else {
            return 0;
        }
        return 1;
    /*
     * The modules that make a stream a terminal.  The slave is one
     * already, so they are there to be found; pushing one that is, or
     * popping it, changes nothing.  A master has none, and a program that
     * asks is told so.
     */
    case SVR4_I_FIND:
        rc = module_name(arg, name);
        *result = rc != 0 ? rc : (is_tty && !is_master && is_tty_module(name));
        return 1;
    case SVR4_I_PUSH:
        rc = module_name(arg, name);
        *result = rc != 0 ? rc
                : (is_tty && is_tty_module(name)) ? 0 : -EINVAL;
        return 1;
    case SVR4_I_POP:
        *result = is_tty && !is_master ? 0 : -EINVAL;
        return 1;
    case SVR4_I_LOOK:
        if (!is_tty || is_master) {
            *result = -EINVAL;
        } else {
            memset(name, 0, sizeof(name));
            memcpy(name, "ttcompat", 8);
            *result = user_put(arg, name, sizeof(name));
        }
        return 1;
    default:
        return is_tty ? ttcompat_ioctl(fd, request, arg, &t, result) : 0;
    }
}

int svr4_ttcompat(int fd, uint32_t request, uint32_t arg, int64_t *result) {
    struct termios t;

    if (kern_ioctl(fd, TCGETS, &t) != 0) {
        return 0;
    }
    return ttcompat_ioctl(fd, request, arg, &t, result);
}
