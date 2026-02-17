#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/signal.h>
#include <vfs/vfs.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_kmem.h>
#include <kern/sched.h>
#include <sys/poll.h>
#include <sys/errno.h>
#include <intr.h>

#define TTY_MAGIC 0x5401

// Locking helper macros
// Note: We use local variable _flags to save interrupt state.
#define TTY_LOCK(tty)   uint32_t _flags = intr_disable(); spinlock_acquire(&(tty)->lock)
#define TTY_UNLOCK(tty) spinlock_release(&(tty)->lock); intr_restore(_flags)

static struct tty *ttys[64]; // Simple array for now

void tty_default_termios(struct termios *t) {
    if (!t) return;
    memset(t, 0, sizeof(struct termios));
    t->c_iflag = ICRNL | IXON;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CREAD | CS8 | HUPCL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    
    t->c_cc[VINTR] = 3;  // ^C
    t->c_cc[VQUIT] = 28; // ^\ (Escaped backslash)
    t->c_cc[VSUSP] = 26; // ^Z
    t->c_cc[VERASE] = 127; // DEL
    t->c_cc[VKILL] = 21; // ^U
    t->c_cc[VEOF] = 4;   // ^D
    t->c_cc[VSTART] = 17; // ^Q
    t->c_cc[VSTOP] = 19;  // ^S
    t->c_cc[VWERASE] = 23; // ^W
}
// ...
// VFS Proxy functions for specific TTY devices
static size_t tty_fs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)offset;
    struct tty *tty = (struct tty *)node->impl;
    return tty_read(tty, (char *)buffer, size);
}

static size_t tty_fs_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)offset;
    struct tty *tty = (struct tty *)node->impl;
    return tty_write(tty, (const char *)buffer, size);
}

static int tty_fs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    struct tty *tty = (struct tty *)node->impl;
    return tty_ioctl(tty, request, (unsigned long)arg);
}

static void tty_fs_open(fs_node_t *node) {
    struct tty *tty = (struct tty *)node->impl;
    tty_open(tty);
}

static void tty_fs_close(fs_node_t *node) {
    struct tty *tty = (struct tty *)node->impl;
    tty_close(tty);
}

// Forward declaration from vfs.h (moved to top)

void tty_register_device(struct tty *tty, char *name) {
    if (!tty || !name) return;
    
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    if (!node) return;
    
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->flags = FS_CHARDEVICE;
    node->impl = (uintptr_t)tty;
    node->read = tty_fs_read;
    node->write = tty_fs_write;
    node->ioctl = tty_fs_ioctl;
    node->open = tty_fs_open;
    node->close = tty_fs_close;
    
    devfs_register_device(node);
}

void tty_init(void) {
    memset(ttys, 0, sizeof(ttys));
}

struct tty *tty_alloc(struct tty_driver *driver, int idx) {
    struct tty *tty = kmalloc(sizeof(struct tty));
    if (!tty) return NULL;
    memset(tty, 0, sizeof(struct tty));
    
    tty->magic = TTY_MAGIC;
    tty->driver = driver;
    tty->index = idx;
    
    spinlock_init(&tty->lock, "tty_lock");

    tty_default_termios(&tty->termios);
    
    tty->winsize.ws_row = 25;
    tty->winsize.ws_col = 80;

    if (driver && driver->install) {
        int ret = driver->install(driver, tty); // Use the signature from main
        if (ret != 0) {
            if (idx >= 0 && idx < 64) ttys[idx] = NULL;
            kfree(tty, sizeof(struct tty));
            return NULL;
        }
    }
    return tty;
}

void tty_free(struct tty *tty) {
    if (!tty) return;
    if (tty->driver && tty->driver->remove) {
        tty->driver->remove(tty->driver, tty); // Use signature from main
    }
    if (tty->index >= 0 && tty->index < 64) {
        ttys[tty->index] = NULL;
    }
    kfree(tty, sizeof(struct tty));
}

// Minimal ring buffer ops
static int tty_buf_put(tty_buffer_t *tb, char c) {
    int next = (tb->head + 1) % TTY_BUF_SIZE;
    if (next != tb->tail) {
        tb->data[tb->head] = c;
        tb->head = next;
        tb->count++;
        return 0; // Success
    }
    return -1; // Full
}

static int tty_buf_get(tty_buffer_t *tb, char *c) {
    if (tb->head == tb->tail) return 0;
    *c = tb->data[tb->tail];
    tb->tail = (tb->tail + 1) % TTY_BUF_SIZE;
    tb->count--;
    return 1;
}

static int tty_canon_len(struct tty *tty) {
    if (!tty) return 0;

    char line[TTY_BUF_SIZE];
    int line_len = 0;
    int idx = tty->raw_buf.tail;
    int count = tty->raw_buf.count;

    for (int i = 0; i < count; i++) {
        char c = tty->raw_buf.data[idx];
        idx = (idx + 1) % TTY_BUF_SIZE;

        if (c == (char)0xFF) {
            line_len = 0;
            continue;
        }

        if (c == tty->termios.c_cc[VERASE]) {
            if (line_len > 0) line_len--;
            continue;
        }

        if (c == tty->termios.c_cc[VKILL]) {
            line_len = 0;
            continue;
        }

        if (c == tty->termios.c_cc[VWERASE]) {
            while (line_len > 0 &&
                (line[line_len - 1] == ' ' || line[line_len - 1] == '\t')) {
                line_len--;
            }
            while (line_len > 0 &&
                (line[line_len - 1] != ' ' && line[line_len - 1] != '\t')) {
                line_len--;
            }
            continue;
        }

        if (c == tty->termios.c_cc[VEOF]) {
            continue;
        }

        if (line_len < TTY_BUF_SIZE) {
            line[line_len++] = c;
        }
    }

    return line_len;
}

// Forward declarations
static void tty_output_locked(char c, struct tty *tp);
static void tty_start_locked(struct tty *tp);

static void tty_send_xchar(struct tty *tp, char c) {
    if (tp->driver->put_char) {
        tp->driver->put_char(tp, c);
    } else if (tp->driver->write) {
        tp->driver->write(tp, (const unsigned char*)&c, 1);
    }
}

// Unix v6-style output processing
static void tty_output_locked(char c, struct tty *tp) {
    // Ignore EOT in normal mode
    if (c == 004 && (tp->termios.c_lflag & ICANON)) return;
    
    // Turn tabs to spaces
    if (c == '\t' && (tp->termios.c_oflag & OXTABS)) { // using OXTABS instead of XTABS
        do {
            tty_output_locked(' ', tp);
        } while (tp->winsize.ws_col && (tp->winsize.ws_col % 8));
        return;
    }
    
    if (c == '\n' && (tp->termios.c_oflag & ONLCR)) {
        tty_output_locked('\r', tp);
    }
    
    // Put to write buffer
    if (tty_buf_put(&tp->write_buf, c) == 0) {
        // v6 delays... we omit delays for modern HW
    } else {
        // Buffer full
        // sleep(&tp->write_buf, ...);
    }
}

static void tty_start_locked(struct tty *tp) {
    if (tp->stopped) return;
    
    if (tp->driver->write) {
        unsigned char buf[128];
        int n;
        while ((n = tp->write_buf.count) > 0) {
            if (n > (int)sizeof(buf)) n = sizeof(buf);
            
            // Check driver write room
            if (tp->driver->write_room) {
                int room = tp->driver->write_room(tp);
                if (room <= 0) break;
                if (n > room) n = room;
            }
            
            // Peek and pull chars
            for (int i = 0; i < n; i++) {
                char c;
                tty_buf_get(&tp->write_buf, &c);
                buf[i] = (unsigned char)c;
            }
            
            int written = tp->driver->write(tp, buf, n);
            if (written <= 0) break; // Driver can't accept more
            
            // If driver wrote less than requested, we'd need to put back chars...
            // but our tty_buf_get already removed them.
            // Simplified: assume driver writes what it can or we pause.
        }
    } else {
        char c;
        while (tty_buf_get(&tp->write_buf, &c)) {
            if (tp->driver->put_char) {
                tp->driver->put_char(tp, c);
            }
        }
    }
    
    if (tp->driver->flush_chars) {
        tp->driver->flush_chars(tp);
    }
    if (tp->driver->flush_chars) {
        tp->driver->flush_chars(tp);
    }
}

// Unix v6-style canonical processing
static void canon(struct tty *tp, uint32_t *flags_ptr) {
    char buf[256]; // Line buffer
    char *bp = buf;
    char c;
    
    // Wait for delimiter
    while (tp->delct == 0) {
        // sleep
        current_thread->wait_chan = &tp->read_wait;
        current_thread->state = THREAD_BLOCKED;

        // Unlock and restore interrupts to sleep
        spinlock_release(&tp->lock);
        intr_restore(*flags_ptr);

        sched_yield();

        // Re-lock and disable interrupts
        *flags_ptr = intr_disable();
        spinlock_acquire(&tp->lock);
    }
    
    while (tty_buf_get(&tp->raw_buf, &c)) {
        if (c == (char)0xFF) { // internal delimiter (0377)
            tp->delct--;
            break; 
        }
        
        if (!(tp->termios.c_lflag & ICANON)) {
            // Raw mode - just pass through
            *bp++ = c;
        } else {
            // Cooked mode
            if (bp > buf && *(bp-1) != '\\') {
                if (c == tp->termios.c_cc[VERASE]) {
                    if (bp > buf) bp--;
                    continue;
                }
                if (c == tp->termios.c_cc[VWERASE]) {
                    while (bp > buf && (*(bp-1) == ' ' || *(bp-1) == '\t')) bp--;
                    while (bp > buf && (*(bp-1) != ' ' && *(bp-1) != '\t')) bp--;
                    continue;
                }
                if (c == tp->termios.c_cc[VKILL]) {
                    bp = buf;
                    continue;
                }
                if (c == tp->termios.c_cc[VEOF]) {
                    continue; // EOF doesn't go into line, just terminates it
                }
            } else {
                 if (c == '\\' && bp > buf && *(bp-1) == '\\') {
                     // escaped backslash? v6 logic:
                     // maptab checks...
                 }
            }
            *bp++ = c;
        }
        
        if (bp >= buf + sizeof(buf) - 1) break; 
    }
    
    // Copy canonical line to read_buf
    char *p = buf;
    while (p < bp) {
        tty_buf_put(&tp->read_buf, *p++);
    }
    
    // Input Flow Control: resume if LWM reached
    if ((tp->termios.c_iflag & IXOFF) && tp->input_stopped) {
        if (tp->raw_buf.count <= (TTY_BUF_SIZE / 4)) {
            tp->input_stopped = 0;
            tty_send_xchar(tp, tp->termios.c_cc[VSTART]);
            if (tp->driver->unthrottle) {
                tp->driver->unthrottle(tp);
            }
        }
    }
}

static void tty_echo(struct tty *tp, unsigned char c) {
    if (!(tp->termios.c_lflag & ECHO)) {
        if (c == '\n' && (tp->termios.c_lflag & ECHONL)) {
            tty_output_locked('\n', tp);
        }
        return;
    }

    if (tp->termios.c_lflag & ICANON) {
        if (c == tp->termios.c_cc[VERASE]) {
            if (tp->termios.c_lflag & ECHOE) {
                tty_output_locked('\b', tp);
                tty_output_locked(' ', tp);
                tty_output_locked('\b', tp);
            } else {
                tty_output_locked(tp->termios.c_cc[VERASE], tp);
            }
            return;
        }
        if (c == tp->termios.c_cc[VKILL]) {
            if (tp->termios.c_lflag & ECHOKE) {
                // Erase line visual
                tty_output_locked('^', tp);
                tty_output_locked('U', tp);
                tty_output_locked('\n', tp);
            } else if (tp->termios.c_lflag & ECHOK) {
                tty_output_locked('\n', tp);
            }
            return;
        }
    }

    if ((tp->termios.c_lflag & ECHOCTL) && c < 32 && c != '\n' && c != '\t') {
        tty_output_locked('^', tp);
        tty_output_locked(c + 64, tp);
        return;
    }

    tty_output_locked(c, tp);
}

// Input processing (ISR context usually)
// Implements 'ttyinput'
void tty_flip_buffer_push(struct tty *tty, char c) {
    if (!tty) return;
    
    TTY_LOCK(tty);

    /* Input Processing */
    if (tty->termios.c_iflag & ISTRIP)
        c &= 0x7F;
        
    if (c == '\r') {
        if (tty->termios.c_iflag & IGNCR) {
            TTY_UNLOCK(tty);
            return;
        }
        if (tty->termios.c_iflag & ICRNL)
            c = '\n';
    } else if (c == '\n') {
        if (tty->termios.c_iflag & INLCR)
            c = '\r';
    }
    
    int raw = !(tty->termios.c_lflag & ICANON);
    
    // Software flow control (IXON) - Output throttling
    if (tty->termios.c_iflag & IXON) {
        if (c == tty->termios.c_cc[VSTOP]) {
            tty->stopped = 1;
            TTY_UNLOCK(tty);
            return;
        }
        if (c == tty->termios.c_cc[VSTART]) {
            tty->stopped = 0;
            tty_start_locked(tty);
            TTY_UNLOCK(tty);
            return;
        }
    }
    
    // Signal handling
    if (!raw && (tty->termios.c_lflag & ISIG)) {
        int sig = 0;
        if (c == tty->termios.c_cc[VINTR]) sig = SIGINT;
        else if (c == tty->termios.c_cc[VQUIT]) sig = SIGQUIT;
        else if (c == tty->termios.c_cc[VSUSP]) sig = SIGTSTP;
        
        if (sig) {
            if (tty->pgrp > 0) signal_send_group(tty->pgrp, sig);
            TTY_UNLOCK(tty);
            return;
        }
    }
    
    int old_canon_len = tty->canon_len;

    // Put char to raw buffer
    tty_buf_put(&tty->raw_buf, c);
    
    // Input Flow Control: stopped if HWM reached
    if ((tty->termios.c_iflag & IXOFF) && !tty->input_stopped) {
        if (tty->raw_buf.count >= (TTY_BUF_SIZE * 3 / 4)) {
            tty->input_stopped = 1;
            tty_send_xchar(tty, tty->termios.c_cc[VSTOP]);
            if (tty->driver->throttle) {
                tty->driver->throttle(tty);
            }
        }
    }
    
    if (raw || c == '\n' || c == tty->termios.c_cc[VEOF]) {
        // Add delimiter
        tty_buf_put(&tty->raw_buf, (char)0xFF);
        tty->delct++;
        sched_wakeup(&tty->read_wait);
    }
    
    // Echo and canonical line tracking
    if (raw) {
        tty_echo(tty, c);
    } else {
        tty->canon_len = tty_canon_len(tty);
        if (c == tty->termios.c_cc[VERASE]) {
            if (old_canon_len > 0) {
                tty_echo(tty, c);
            }
        } else if (c == tty->termios.c_cc[VWERASE]) {
            if (old_canon_len > 0) {
                tty_echo(tty, c);
            }
        } else {
            tty_echo(tty, c);
        }
    }
    tty_start_locked(tty);

    TTY_UNLOCK(tty);
}

// Job Control Checks
static int tty_check_read(struct tty *tty) {
    if (!tty) return -1;
    if (tty->pgrp <= 0) return 0; // No foreground group
    
    // Check if current process is in background
    int cur_pgrp = (current_process->p_pgrp) ? current_process->p_pgrp->pg_id : 0;
    if (cur_pgrp != tty->pgrp) {
        // Send SIGTTIN
        // In POSIX, if process is ignored/blocked SIGTTIN, read returns EIO?
        // For now, simpler implementation:
        if (tty->pgrp > 0 && cur_pgrp > 0)
            signal_send_group(cur_pgrp, SIGTTIN);
        // We should suspend here or return error so signal handler runs
        return 1; // Signal sent
    }
    return 0; 
}

static int tty_check_write(struct tty *tty) {
    if (!tty) return -1;
    if (tty->pgrp <= 0) return 0;
    
    // TOSTOP flag check
    if (!(tty->termios.c_lflag & TOSTOP)) return 0;
    
    int cur_pgrp = (current_process->p_pgrp) ? current_process->p_pgrp->pg_id : 0;
    if (cur_pgrp != tty->pgrp) {
        if (tty->pgrp > 0 && cur_pgrp > 0)
            signal_send_group(cur_pgrp, SIGTTOU);
        return 1; // Signal sent
    }
    return 0;
}

int tty_read(struct tty *tty, char *buf, int len) {
    if (!tty || len <= 0) return 0;
    
    TTY_LOCK(tty);

    if (tty_check_read(tty)) {
        // Should restart syscall or return EINTR
        // For now return 0 or -1?
        // Returning 0 might be interpreted as EOF.
        // Returning -1 and setting errno = EINTR is correct.
        // But we don't have errno access here easily.
        // Assume syscall wrapper handles signal interruption.
        TTY_UNLOCK(tty);
        return -4; // EINTR 
    }
    
    // If read_buf is empty, canonicalize a line
    // Loop helps handle partial reads or retries
    int count = 0;
    
    while (count < len) {
        if (tty->read_buf.head == tty->read_buf.tail) {
            canon(tty, &_flags); // Blocks until line available. Releases/reacquires lock.
        }
        
        while (count < len) {
            char c;
            if (tty_buf_get(&tty->read_buf, &c)) {
                buf[count++] = c;
                // In canonical mode, return on newline
                if ((tty->termios.c_lflag & ICANON) && c == '\n') {
                    TTY_UNLOCK(tty);
                    return count;
                }
            } else {
                break; // read_buf empty, need more canon
            }
        }
        
        if (tty->termios.c_lflag & ICANON) {
             if (count > 0) {
                 TTY_UNLOCK(tty);
                 return count; // Return processed line
             }
        } else {
             // Raw mode: return whatever we got
             if (count > 0) {
                 TTY_UNLOCK(tty);
                 return count;
             }
        }
    }
    TTY_UNLOCK(tty);
    return count;
}

int tty_write(struct tty *tty, const char *buf, int len) {
    if (!tty) return 0;
    
    TTY_LOCK(tty);

    if (tty_check_write(tty)) {
        TTY_UNLOCK(tty);
        return -4; // EINTR
    }
    
    for (int i = 0; i < len; i++) {
        tty_output_locked(buf[i], tty);
    }
    tty_start_locked(tty);
    TTY_UNLOCK(tty);
    return len;
}

int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    if (!tty) return -1;

    struct termios k_termios;
    struct winsize k_winsize;
    int k_int;
    int (*driver_ioctl)(struct tty *, uint32_t, unsigned long) = NULL;
    int ret = -1;

    /*
     * Copy-in phase (unlocked)
     * We must not hold spinlocks while accessing user memory via copyin/copyout.
     */
    switch (cmd) {
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            if (copyin((void*)arg, &k_termios, sizeof(struct termios)) != 0) return -EFAULT;
            break;
        case TIOCSWINSZ:
            if (copyin((void*)arg, &k_winsize, sizeof(struct winsize)) != 0) return -EFAULT;
            break;
        case TIOCSPGRP:
            if (copyin((void*)arg, &k_int, sizeof(int)) != 0) return -EFAULT;
            break;
    }

    TTY_LOCK(tty);

    switch (cmd) {
        case TCGETS:
            k_termios = tty->termios;
            ret = 0;
            break;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            tty->termios = k_termios;
            if (tty->driver->set_termios) tty->driver->set_termios(tty);
            ret = 0;
            break;
        case TIOCGWINSZ:
            k_winsize = tty->winsize;
            ret = 0;
            break;
        case TIOCSWINSZ:
            tty->winsize = k_winsize;
            if (tty->pgrp > 0) signal_send_group(tty->pgrp, SIGWINCH);
            ret = 0;
            break;
        case TIOCSPGRP:
            tty->pgrp = k_int;
            ret = 0;
            break;
        case TIOCGPGRP:
            k_int = tty->pgrp;
            ret = 0;
            break;
        case TIOCSCTTY: {
            // Set controlling TTY
            // Arg: 0 or 1. If 1, steal from another session.
            // Must be session leader: check if p_pgrp->pg_session->s_leader == self
            int is_session_leader = 0;
            if (current_process->p_pgrp && 
                current_process->p_pgrp->pg_session &&
                current_process->p_pgrp->pg_session->s_leader == current_process) {
                is_session_leader = 1;
            }
            if (!is_session_leader) {
                // Must be session leader
                // return EPERM;
                ret = -1;
                break;
            }
            // If already has ctty and arg!=1, fail?
            // Simplified: Just set it.
            if (current_process->tty == NULL || arg == 1) {
                int cur_sid = current_process->p_pgrp->pg_session->s_sid;
                int cur_pgrp = current_process->p_pgrp->pg_id;
                tty->session = cur_sid;
                tty->pgrp = cur_pgrp;
                // current_process->tty = fs_node... (Cannot set fs_node here directly without context)
                // Assuming VFS layer calls this and updates process->tty if success.
                ret = 0;
                break;
            }
            ret = -1;
            break;
        }
        
        case TIOCNOTTY: {
            // Detach ctty
            int cur_sid = 0;
            if (current_process->p_pgrp && current_process->p_pgrp->pg_session) {
                cur_sid = current_process->p_pgrp->pg_session->s_sid;
            }
            if (tty->session == cur_sid) {
                tty->session = 0;
                tty->pgrp = 0;
                // current_process->tty = NULL;
                ret = 0;
                break;
            }
            ret = -1;
            break;
        }
    }

    /*
     * Call driver ioctl if not handled.
     * We unlock before calling the driver to allow it to copyin/copyout safely.
     * The driver is responsible for its own locking if it needs to access tty internals.
     */
    if (ret == -1 && tty->driver && tty->driver->ioctl) {
        driver_ioctl = tty->driver->ioctl;
    }

    TTY_UNLOCK(tty);

    if (driver_ioctl) {
        ret = driver_ioctl(tty, cmd, arg);
    }

    /* Copy-out phase (unlocked) */
    if (ret == 0) {
        switch (cmd) {
            case TCGETS:
                if (copyout(&k_termios, (void*)arg, sizeof(struct termios)) != 0) return -EFAULT;
                break;
            case TIOCGWINSZ:
                if (copyout(&k_winsize, (void*)arg, sizeof(struct winsize)) != 0) return -EFAULT;
                break;
            case TIOCGPGRP:
                if (copyout(&k_int, (void*)arg, sizeof(int)) != 0) return -EFAULT;
                break;
        }
    }

    return ret;
}

int tty_open(struct tty *tty) {
    if (!tty) return -1;
    TTY_LOCK(tty);
    tty->count++;
    if (tty->driver->open) {
        int ret = tty->driver->open(tty);
        if (ret != 0) {
            tty->count--;
        }
        TTY_UNLOCK(tty);
        return ret;
    }
    TTY_UNLOCK(tty);
    return 0;
}

void tty_close(struct tty *tty) {
    if (!tty) return;
    TTY_LOCK(tty);
    tty->count--;
    if (tty->count <= 0) {
        if (tty->driver->close) tty->driver->close(tty);
    }
    TTY_UNLOCK(tty);
}

/*
 * tty_hangup - Handle terminal hangup
 *
 * Called when the controlling terminal is disconnected (modem hangup,
 * master side of pty closed, etc.). Sends SIGHUP to the foreground 
 * process group, then SIGCONT to ensure stopped processes receive it.
 *
 * Per POSIX, on terminal hangup:
 * 1. SIGHUP is sent to the foreground process group
 * 2. The terminal is disassociated from the session
 */
void tty_hangup(struct tty *tty) {
    if (!tty) return;
    
    TTY_LOCK(tty);

    /* Send SIGHUP to foreground process group */
    if (tty->pgrp > 0) {
        signal_send_group(tty->pgrp, SIGHUP);
        /* Also send SIGCONT to wake any stopped processes so they can
         * receive SIGHUP (stopped jobs won't process signals until continued) */
        signal_send_group(tty->pgrp, SIGCONT);
    }
    
    /* Disassociate terminal from session */
    tty->session = 0;
    tty->pgrp = 0;

    TTY_UNLOCK(tty);
}

int tty_poll(struct tty *tty, void *waiter) {
    if (!tty) return POLLNVAL;
    (void)waiter; // No wait queue support yet
    
    int events = 0;
    
    TTY_LOCK(tty);

    // Check for input
    if (tty->read_buf.head != tty->read_buf.tail) {
        events |= POLLIN | POLLRDNORM;
    } else if (tty->delct > 0) {
        // We have a delimiter in raw buf, so read will succeed (after canon runs).
        events |= POLLIN | POLLRDNORM;
    }
    
    // Check for writability
    int write_room = TTY_BUF_SIZE - tty->write_buf.count;
    int pending = tty->write_buf.count;
    if (tty->driver->write_room) {
        write_room = tty->driver->write_room(tty);
    }
    if (tty->driver->chars_in_buffer) {
        pending += tty->driver->chars_in_buffer(tty);
    }
    if (write_room > 0 && pending < TTY_BUF_SIZE) {
        events |= POLLOUT | POLLWRNORM;
    }
    TTY_UNLOCK(tty);
    return events;
}
