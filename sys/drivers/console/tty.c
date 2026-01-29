#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/signal.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_kmem.h>
#include <kern/sched.h>
#include <sys/poll.h>

#define TTY_MAGIC 0x5401

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
void tty_register_device(struct tty *tty, char *name) {
    (void)tty; (void)name;
    // Wrapper for devfs registration
    // Not implemented fully yet, need fs_node creation
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
    
    tty_default_termios(&tty->termios);
    
    tty->winsize.ws_row = 25;
    tty->winsize.ws_col = 80;
    
    if (idx >= 0 && idx < 64) {
        ttys[idx] = tty;
    }
    
    return tty;
}

void tty_free(struct tty *tty) {
    if (tty) kfree(tty, sizeof(struct tty));
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

// Forward declarations
void ttyoutput(char c, struct tty *tp);
void ttstart(struct tty *tp);

static void tty_send_xchar(struct tty *tp, char c) {
    if (tp->driver->put_char) {
        tp->driver->put_char(tp, c);
    } else if (tp->driver->write) {
        tp->driver->write(tp, (const unsigned char*)&c, 1);
    }
}

// Unix v6-style output processing
void ttyoutput(char c, struct tty *tp) {
    // Ignore EOT in normal mode
    if (c == 004 && (tp->termios.c_lflag & ICANON)) return;
    
    // Turn tabs to spaces
    if (c == '\t' && (tp->termios.c_oflag & OXTABS)) { // using OXTABS instead of XTABS
        do {
            ttyoutput(' ', tp);
        } while (tp->winsize.ws_col && (tp->winsize.ws_col % 8)); // Very approx column tracking needed?
        // Note: keeping state of col requires explicit col tracking in struct tty.
        // For now, simpler implementation or assume hardware handles it if we don't.
        // But requested to match v6. v6 tracks t_col.
        // We lack t_col in struct tty (winsize is for size, not cursor).
        return;
    }
    
    if (c == '\n' && (tp->termios.c_oflag & ONLCR)) {
        ttyoutput('\r', tp);
    }
    
    // Put to write buffer
    if (tty_buf_put(&tp->write_buf, c) == 0) {
        // v6 delays... we omit delays for modern HW
    } else {
        // Buffer full
        // sleep(&tp->write_buf, ...);
    }
}

void ttstart(struct tty *tp) {
    if (tp->stopped) return;
    char c;
    while (tty_buf_get(&tp->write_buf, &c)) {
        if (tp->driver->put_char) {
            tp->driver->put_char(tp, c);
        } else if (tp->driver->write) {
            tp->driver->write(tp, (unsigned char*)&c, 1);
        }
    }
}

// Unix v6-style canonical processing
static void canon(struct tty *tp) {
    char buf[256]; // Line buffer
    char *bp = buf;
    char c;
    
    // Wait for delimiter
    while (tp->delct == 0) {
        // sleep
        current_thread->wait_chan = &tp->read_wait;
        current_thread->state = THREAD_BLOCKED;
        sched_yield();
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
        }
    }
}

// Input processing (ISR context usually)
// Implements 'ttyinput'
void tty_flip_buffer_push(struct tty *tty, char c) {
    if (!tty) return;
    
    /* Input Processing */
    if (tty->termios.c_iflag & ISTRIP)
        c &= 0x7F;
        
    if (c == '\r') {
        if (tty->termios.c_iflag & IGNCR)
            return;
        if (tty->termios.c_iflag & ICRNL)
            c = '\n';
    } else if (c == '\n') {
        if (tty->termios.c_iflag & INLCR)
            c = '\r';
    }
    
    // cooked = !(RAW)
    // In v6, flags&RAW checks.
    
    int raw = !(tty->termios.c_lflag & ICANON);
    
    // Software flow control (IXON) - Output throttling
    if (tty->termios.c_iflag & IXON) {
        if (c == tty->termios.c_cc[VSTOP]) {
            tty->stopped = 1;
            return; /* Do not pass VSTOP to application */
        }
        if (c == tty->termios.c_cc[VSTART]) {
            tty->stopped = 0;
            ttstart(tty); /* Kick output */
            return; /* Do not pass VSTART to application */
        }
    }
    
    // Signal handling
    if (!raw && (tty->termios.c_lflag & ISIG)) {
        int sig = 0;
        if (c == tty->termios.c_cc[VINTR]) sig = SIGINT;
        else if (c == tty->termios.c_cc[VQUIT]) sig = SIGQUIT;
        else if (c == tty->termios.c_cc[VSUSP]) sig = SIGTSTP; /* Job control stop (Ctrl-Z) */
        
        if (sig) {
            if (tty->pgrp > 0) signal_send_group(tty->pgrp, sig);
            // v6: flushtty(tp)
            return;
        }
    }
    
    // Put char to raw buffer
    tty_buf_put(&tty->raw_buf, c);
    
    // Input Flow Control: stopped if HWM reached
    if ((tty->termios.c_iflag & IXOFF) && !tty->input_stopped) {
        if (tty->raw_buf.count >= (TTY_BUF_SIZE * 3 / 4)) {
            tty->input_stopped = 1;
            tty_send_xchar(tty, tty->termios.c_cc[VSTOP]);
        }
    }
    
    if (raw || c == '\n' || c == tty->termios.c_cc[VEOF]) {
        // Add delimiter
        tty_buf_put(&tty->raw_buf, (char)0xFF);
        tty->delct++;
        sched_wakeup(&tty->read_wait);
    }
    
    // Echo
    if (tty->termios.c_lflag & ECHO) {
    ttyoutput(c, tty); // Echoes through canonical output processing!
        ttstart(tty);
    }
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
    
    if (tty_check_read(tty)) {
        // Should restart syscall or return EINTR
        // For now return 0 or -1?
        // Returning 0 might be interpreted as EOF.
        // Returning -1 and setting errno = EINTR is correct.
        // But we don't have errno access here easily.
        // Assume syscall wrapper handles signal interruption.
        return -4; // EINTR 
    }
    
    // If read_buf is empty, canonicalize a line
    // Loop helps handle partial reads or retries
    int count = 0;
    
    while (count < len) {
        if (tty->read_buf.head == tty->read_buf.tail) {
            canon(tty); // Blocks until line available
        }
        
        while (count < len) {
            char c;
            if (tty_buf_get(&tty->read_buf, &c)) {
                buf[count++] = c;
                // In canonical mode, return on newline
                if ((tty->termios.c_lflag & ICANON) && c == '\n') return count;
            } else {
                break; // read_buf empty, need more canon
            }
        }
        
        if (tty->termios.c_lflag & ICANON) {
             if (count > 0) return count; // Return processed line
        } else {
             // Raw mode: return whatever we got
             if (count > 0) return count;
        }
    }
    return count;
}

int tty_write(struct tty *tty, const char *buf, int len) {
    if (!tty) return 0;
    
    if (tty_check_write(tty)) {
        return -4; // EINTR
    }
    
    for (int i = 0; i < len; i++) {
        ttyoutput(buf[i], tty);
    }
    ttstart(tty);
    return len;
}

int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    if (!tty) return -1;
    
    switch (cmd) {
        case TCGETS:
            if (arg) memcpy((void*)arg, &tty->termios, sizeof(struct termios));
            return 0;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            if (arg) memcpy(&tty->termios, (void*)arg, sizeof(struct termios));
            return 0;
        case TIOCGWINSZ:
            if (arg) memcpy((void*)arg, &tty->winsize, sizeof(struct winsize));
            return 0;
        case TIOCSPGRP:
            if (arg) tty->pgrp = *(int*)arg;
            return 0;
        case TIOCGPGRP:
            if (arg) *(int*)arg = tty->pgrp;
            return 0;
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
                return -1;
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
                return 0;
            }
            return -1;
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
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

int tty_open(struct tty *tty) {
    if (!tty) return -1;
    tty->count++;
    if (tty->driver->open) return tty->driver->open(tty);
    return 0;
}

void tty_close(struct tty *tty) {
    if (!tty) return;
    tty->count--;
    if (tty->count <= 0) {
        if (tty->driver->close) tty->driver->close(tty);
    }
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
}

int tty_poll(struct tty *tty, void *waiter) {
    if (!tty) return POLLNVAL;
    (void)waiter; // No wait queue support yet
    
    int events = 0;
    
    // Check for input
    // If ICANON, check read_buf (canonical lines).
    // If !ICANON, check raw_buf (or whatever logic tty_read uses).
    // Logic in tty_read:
    // If read_buf.head != read_buf.tail, we have data.
    // If !ICANON, scanning raw_buf might be needed? 
    // Wait, canon() moves raw -> read.
    // Actually, tty_read checks read_buf. If empty, calls canon().
    // canon() waits for delimiter.
    
    // So POLLIN is true if:
    // 1. read_buf is not empty.
    // 2. OR (if !ICANON) raw_buf is not empty? 
    //    tty_flip_buffer_push puts to raw_buf.
    //    If !ICANON, canon() passes through immediately logic?
    //    Let's check tty_read again. 
    //    tty_read calls canon() if read_buf empty.
    //    canon() waits for delimiter.
    //    Use tty->delct? tty->delct > 0 means we have a line/delimiter.
    
    if (tty->read_buf.head != tty->read_buf.tail) {
        events |= POLLIN | POLLRDNORM;
    } else if (tty->delct > 0) {
        // We have a delimiter in raw buf, so read will succeed (after canon runs).
        // Since we can't run canon here (it might block?), we assume readable.
        // Actually, if we are here, we should trigger canon? 
        // No, poll shouldn't change state.
        // But if delct > 0, it means we HAVE a line ready to be processed.
        events |= POLLIN | POLLRDNORM;
    }
    
    // Always writable for now
    events |= POLLOUT | POLLWRNORM;
    
    return events;
}
