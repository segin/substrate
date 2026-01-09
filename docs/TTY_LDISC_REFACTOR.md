# TTY Line Discipline Refactoring Notes

*Saved from ChatGPT analysis - 2026-01-08*
*Delete this file after processing*

---

## Analysis Summary

The current TTY implementation is **original/clean-room** - follows POSIX semantics, not copied from any specific kernel. It works but collapses all line discipline logic into `tty_flip_buffer_push()`.

---

## Current Issues Identified

### 1. Signal handling doesn't flush read buffer
- Currently echoes `^C` and newline immediately  
- Does NOT flush the read buffer on `VINTR` / `VQUIT`
- POSIX requires different semantics depending on flags

### 2. TCSETS variants treated identically
```c
case TCSETS:
case TCSETSW:
case TCSETSF:
    memcpy(&tty->termios, ...)
```
- `TCSETSW` should drain output first
- `TCSETSF` should flush input and drain output
- Current implementation ignores these semantics

### 3. No pluggable line discipline layer
- Fine for now, but limits extensibility
- Cannot support PPP, SLIP, multiple PTYs easily

---

## Proposed Line Discipline Interface

```c
struct tty_ldisc {
    const char *name;

    void (*open)(struct tty *);
    void (*close)(struct tty *);

    /* input path: driver → ldisc */
    void (*receive_char)(struct tty *, char);

    /* output path: userspace → ldisc → driver */
    int  (*write)(struct tty *, const char *, int);

    /* userspace read */
    int  (*read)(struct tty *, char *, int);

    /* ioctl passthrough / override */
    int  (*ioctl)(struct tty *, uint32_t, unsigned long);
};
```

Add to `struct tty`:
```c
struct tty {
    ...
    struct tty_ldisc *ldisc;
    void *ldisc_data;   // per-discipline private state
};
```

---

## Data Flow After Refactor

### Input path
```
interrupt / poll
   ↓
tty_driver->receive_char()
   ↓
tty->ldisc->receive_char()
   ↓
ldisc buffering / editing / signals
   ↓
wake readers
```

### Output path
```
write()
   ↓
tty->ldisc->write()
   ↓
output processing (OPOST, ONLCR)
   ↓
tty_driver->write()
```

---

## N_TTY Canonical Discipline Implementation

### Private state
```c
struct n_tty {
    tty_buffer_t canon_buf;   // cooked input
};
```

### Input processing (replaces tty_flip_buffer_push)
```c
static void n_tty_receive_char(struct tty *tty, char c) {
    struct n_tty *nt = tty->ldisc_data;

    /* ISIG handling */
    if (tty->termios.c_lflag & ISIG) {
        int sig = 0;

        if (c == tty->termios.c_cc[VINTR]) sig = SIGINT;
        else if (c == tty->termios.c_cc[VQUIT]) sig = SIGQUIT;
        else if (c == tty->termios.c_cc[VSUSP]) sig = SIGTSTP;

        if (sig) {
            tty_kill_line(nt);  // FLUSH THE BUFFER
            signal_send_group(tty->pgrp, sig);
            tty_echo_control(tty, c);
            return;
        }
    }

    if (tty->termios.c_lflag & ICANON) {
        n_tty_canonical_input(tty, nt, c);
    } else {
        tty_buf_put(&tty->read_buf, c);
        if (tty->termios.c_lflag & ECHO)
            tty_echo_char(tty, c);
        sched_wakeup(&tty->read_wait);
    }
}
```

### Canonical editing
```c
static void n_tty_canonical_input(struct tty *tty, struct n_tty *nt, char c) {
    if (c == '\n' || c == '\r') {
        tty_buf_put(&tty->read_buf, '\n');
        tty_echo_char(tty, '\n');
        sched_wakeup(&tty->read_wait);
        return;
    }

    if (c == tty->termios.c_cc[VERASE]) {
        if (!tty_line_empty(nt)) {
            tty_line_erase(nt);
            tty_echo_erase(tty);
        }
        return;
    }

    tty_buf_put(&tty->read_buf, c);
    tty_echo_char(tty, c);
}
```

### Output processing via discipline
```c
static int n_tty_write(struct tty *tty, const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        char c = buf[i];

        if ((tty->termios.c_oflag & OPOST) &&
            (c == '\n') &&
            (tty->termios.c_oflag & ONLCR)) {
            tty->driver->put_char(tty, '\r');
        }

        tty->driver->put_char(tty, c);
    }
    return len;
}
```

---

## Discipline Switching
```c
int tty_set_ldisc(struct tty *tty, struct tty_ldisc *new) {
    if (tty->ldisc && tty->ldisc->close)
        tty->ldisc->close(tty);

    tty->ldisc = new;

    if (new && new->open)
        new->open(tty);

    return 0;
}
```

---

## Implementation Order

1. **Refactor `tty_flip_buffer_push()` into `ldisc->receive_char()`**
2. **Move output processing out of `tty_write()` into the discipline**
3. Fix signal handling to flush buffer on SIGINT/SIGQUIT
4. Implement proper TCSETSW/TCSETSF semantics

---

## Future Enhancements

- PTY master/slave semantics
- Job control correctness  
- Foreground process enforcement
- `select()` / `poll()` hooks

---

## Design Properties

- POSIX-aligned
- Not Linux-specific
- Not over-engineered
- Safe from plagiarism concerns
- Clean separation of concerns
- Works with PTYs naturally
