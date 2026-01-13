#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <string.h>
#include <stdio.h>
#include "../vm/vm_kmem.h" // for kmalloc
#include "../kern/sched.h" // for sched_wakeup, sleep

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
}

// Input processing (ISR context usually)
// Implements 'ttyinput'
void tty_flip_buffer_push(struct tty *tty, char c) {
    if (!tty) return;
    
    // cooked = !(RAW)
    // In v6, flags&RAW checks.
    
    int raw = !(tty->termios.c_lflag & ICANON);
    
    // Signal handling
    if (!raw && (tty->termios.c_lflag & ISIG)) {
        int sig = 0;
        if (c == tty->termios.c_cc[VINTR]) sig = SIGINT;
        else if (c == tty->termios.c_cc[VQUIT]) sig = SIGQUIT;
        
        if (sig) {
            if (tty->pgrp > 0) signal_send_group(tty->pgrp, sig);
            // v6: flushtty(tp)
            return;
        }
    }
    
    // Put char to raw buffer
    tty_buf_put(&tty->raw_buf, c);
    
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
    if (current_process->pgrp != tty->pgrp) {
        // Send SIGTTIN
        // In POSIX, if process is ignored/blocked SIGTTIN, read returns EIO?
        // For now, simpler implementation:
        if (tty->pgrp > 0)
            signal_send_group(current_process->pgrp, SIGTTIN);
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
    
    if (current_process->pgrp != tty->pgrp) {
        if (tty->pgrp > 0)
            signal_send_group(current_process->pgrp, SIGTTOU);
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
        case TIOCSCTTY:
            // Set controlling TTY
            // Arg: 0 or 1. If 1, steal from another session.
            if (current_process->session != current_process->pid) {
                // Must be session leader
                // return EPERM;
                return -1;
            }
            // If already has ctty and arg!=1, fail?
            // Simplified: Just set it.
            if (current_process->tty == NULL || arg == 1) {
                // Assuming we can map tty structure to fs_node?
                // This function gets 'struct tty'. We need 'fs_node'.
                // Ideally tty_register has linked them.
                // For now, we set the session ID in tty logic.
                tty->session = current_process->session;
                tty->pgrp = current_process->pgrp;
                // current_process->tty = fs_node... (Cannot set fs_node here directly without context)
                // Assuming VFS layer calls this and updates process->tty if success.
                return 0;
            }
            return -1;
        
        case TIOCNOTTY:
            // Detach ctty
            if (tty->session == current_process->session) {
                tty->session = 0;
                tty->pgrp = 0;
                // current_process->tty = NULL;
                return 0;
            }
            return -1;
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
