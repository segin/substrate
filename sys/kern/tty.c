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
    if (tty) kmfree(tty);
}

// Minimal ring buffer ops
static void tty_buf_put(tty_buffer_t *tb, char c) {
    int next = (tb->head + 1) % TTY_BUF_SIZE;
    if (next != tb->tail) {
        tb->data[tb->head] = c;
        tb->head = next;
        tb->count++;
    }
}

static int tty_buf_get(tty_buffer_t *tb, char *c) {
    if (tb->head == tb->tail) return 0;
    *c = tb->data[tb->tail];
    tb->tail = (tb->tail + 1) % TTY_BUF_SIZE;
    tb->count--;
    return 1;
}

static int tty_buf_pop_back(tty_buffer_t *tb) {
    if (tb->head == tb->tail) return 0;
    // Move head back one
    tb->head = (tb->head - 1 + TTY_BUF_SIZE) % TTY_BUF_SIZE;
    tb->count--;
    return 1;
}

static void echo_char(struct tty *tty, char c) {
    if (!(tty->termios.c_lflag & ECHO)) return;
    
    if (c == '\n' && (tty->termios.c_oflag & ONLCR)) {
        if (tty->driver->write) tty->driver->write(tty, (unsigned char*)"\r\n", 2);
    } else if (c == 127 || c == '\b') {
        if (tty->driver->write) tty->driver->write(tty, (unsigned char*)"\b \b", 3);
    } else {
        if (tty->driver->put_char) tty->driver->put_char(tty, c);
        else if (tty->driver->write) tty->driver->write(tty, (unsigned char*)&c, 1);
    }
}

// Input processing line discipline
void tty_flip_buffer_push(struct tty *tty, char c) {
    if (!tty) return;
    
    // Handle specific control chars if ISIG
    if (tty->termios.c_lflag & ISIG) {
        int sig = 0;
        if (c == tty->termios.c_cc[VINTR]) { // ^C
            sig = SIGINT;
        } else if (c == tty->termios.c_cc[VQUIT]) { // Control-Quit
            sig = SIGQUIT;
        } else if (c == tty->termios.c_cc[VSUSP]) { // ^Z
            sig = SIGTSTP;
        }

        if (sig != 0) {
            if (tty->pgrp > 0) signal_send_group(tty->pgrp, sig);
            echo_char(tty, '^');
            echo_char(tty, c + 64); // '@' for 0, 'A' for 1...
            echo_char(tty, '\n');
            return;
        }
    }
    
    // Canonical mode processing
    if (tty->termios.c_lflag & ICANON) {
        if (c == '\n' || c == '\r') {
            tty_buf_put(&tty->read_buf, '\n');
            echo_char(tty, '\n');
            sched_wakeup(&tty->read_wait);
        } else if (c == 127 || c == '\b') { // Backspace
             if (tty->read_buf.count > 0 && tty->read_buf.data[(tty->read_buf.head - 1 + TTY_BUF_SIZE) % TTY_BUF_SIZE] != '\n') {
                 tty_buf_pop_back(&tty->read_buf);
                 echo_char(tty, c);
             }
        } else {
            tty_buf_put(&tty->read_buf, c);
            echo_char(tty, c);
        }
    } else {
        // Raw mode
        tty_buf_put(&tty->read_buf, c);
        if (tty->termios.c_lflag & ECHO) echo_char(tty, c);
        sched_wakeup(&tty->read_wait);
    }
}

int tty_read(struct tty *tty, char *buf, int len) {
    if (!tty || len <= 0) return 0;
    
    int i = 0;
    while (i < len) {
        char c;
        if (tty_buf_get(&tty->read_buf, &c)) {
            buf[i++] = c;
            // In canonical mode, return on newline
            if ((tty->termios.c_lflag & ICANON) && c == '\n') break;
        } else {
            // Buffer empty, wait
            if (i > 0) break; // Return what we have
             // Should check O_NONBLOCK here
             
             // extern void sleep_on(void *chan);
             current_thread->wait_chan = &tty->read_wait;
             current_thread->state = THREAD_BLOCKED;
             sched_yield();
        }
    }
    return i;
}

int tty_write(struct tty *tty, const char *buf, int len) {
    if (!tty || !tty->driver || !tty->driver->write) return 0;
    
    // Output processing
    if (tty->termios.c_oflag & OPOST) {
        for (int i = 0; i < len; i++) {
            char c = buf[i];
            if (c == '\n' && (tty->termios.c_oflag & ONLCR)) {
                 tty->driver->write(tty, (unsigned char*)"\r", 1);
            }
            tty->driver->write(tty, (unsigned char*)&c, 1);
        }
        return len;
    } else {
        return tty->driver->write(tty, (const unsigned char*)buf, len);
    }
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
            // TODO: Update process session to point to this TTY
            if (current_process) {
                // current_process->tty = tty_node... need mapping back to node or just hold logic
                // If we have fs_node in tty or vice versa.
            }
            return 0;
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
