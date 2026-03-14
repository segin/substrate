#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pm/pm.h>
#include <sys/tty.h>
#include <sys/session.h>
#include <sys/signal.h>
#include <sys/copy.h>

#define _ARCH_I386_INTR_H
process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static int signal_count;
static int signal_pgrp[8];
static int signal_sig[8];
static process_t *last_psignal_proc;
static int last_psignal_sig;
static int tty_driver_open_count;
static int tty_driver_close_count;
static int tty_driver_open_errno;
static int tty_driver_install_count;
static int tty_driver_remove_count;
static int tty_driver_flush_count;
static unsigned char tty_driver_out[256];
static int tty_driver_out_len;
static fs_node_t *last_devfs_node;
static int tty_driver_throttle_count;
static int tty_driver_unthrottle_count;

static int mock_tty_write(struct tty *tty, const unsigned char *buf, int count);
static int mock_tty_write_room(struct tty *tty);
static int mock_tty_put_char(struct tty *tty, unsigned char c);
static void mock_tty_throttle(struct tty *tty);
static void mock_tty_unthrottle(struct tty *tty);

uint32_t intr_disable(void) { return 0; }
void intr_restore(uint32_t flags) { (void)flags; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
int copyin(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyout(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    size_t n = strnlen((const char *)src, maxlen);
    if (n == maxlen) return -1;
    memcpy(dst, src, n + 1);
    if (len) *len = n + 1;
    return 0;
}
void devfs_register_device(fs_node_t *node) { last_devfs_node = node; }
void kprint(const char *str) { (void)str; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_yield(void) {}
int signal_send_group(int pgrp, int sig) {
    if (signal_count < 8) {
        signal_pgrp[signal_count] = pgrp;
        signal_sig[signal_count] = sig;
    }
    signal_count++;
    return 0;
}
void psignal(process_t *proc, int sig) {
    last_psignal_proc = proc;
    last_psignal_sig = sig;
}

#include "../../sys/drivers/console/tty.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    current_process = NULL;
    current_thread = NULL;
    signal_count = 0;
    memset(signal_pgrp, 0, sizeof(signal_pgrp));
    memset(signal_sig, 0, sizeof(signal_sig));
    last_psignal_proc = NULL;
    last_psignal_sig = 0;
    tty_driver_open_count = 0;
    tty_driver_close_count = 0;
    tty_driver_open_errno = 0;
    tty_driver_install_count = 0;
    tty_driver_remove_count = 0;
    tty_driver_flush_count = 0;
    tty_driver_out_len = 0;
    memset(tty_driver_out, 0, sizeof(tty_driver_out));
    last_devfs_node = NULL;
    tty_driver_throttle_count = 0;
    tty_driver_unthrottle_count = 0;
}

static void test_tty_init_clears_global_slots(void) {
    reset_env();

    ttys[0] = (struct tty *)0x1;
    ttys[1] = (struct tty *)0x2;

    tty_init();

    assert(ttys[0] == NULL);
    assert(ttys[1] == NULL);
}

static void test_tty_register_device_publishes_devfs_node(void) {
    struct tty_driver driver = {0};
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 0);
    assert(tty != NULL);

    tty_register_device(tty, "tty42");

    assert(last_devfs_node != NULL);
    assert(tty->devnode == last_devfs_node);
    assert(strcmp(last_devfs_node->name, "tty42") == 0);
    assert(last_devfs_node->flags == FS_CHARDEVICE);
    assert(last_devfs_node->mask == 0666);
    assert(last_devfs_node->uid == 0);
    assert(last_devfs_node->gid == 0);
    assert(last_devfs_node->ptr == (fs_node_t *)tty);
    assert(last_devfs_node->read == tty_fs_read);
    assert(last_devfs_node->write == tty_fs_write);
    assert(last_devfs_node->ioctl == tty_fs_ioctl);
    assert(last_devfs_node->open == tty_fs_open);
    assert(last_devfs_node->close == tty_fs_close);
}

static void test_tty_raw_buffer_wraps_as_circular_queue(void) {
    struct tty tty;
    char c;

    reset_env();
    memset(&tty, 0, sizeof(tty));

    tty.raw_buf.head = TTY_BUF_SIZE - 1;
    tty.raw_buf.tail = TTY_BUF_SIZE - 1;

    assert(tty_buf_put(&tty.raw_buf, 'A') == 0);
    assert(tty.raw_buf.head == 0);
    assert(tty.raw_buf.tail == TTY_BUF_SIZE - 1);
    assert(tty.raw_buf.count == 1);

    assert(tty_buf_put(&tty.raw_buf, 'B') == 0);
    assert(tty.raw_buf.head == 1);
    assert(tty.raw_buf.count == 2);

    assert(tty_buf_get(&tty.raw_buf, &c) == 1);
    assert(c == 'A');
    assert(tty.raw_buf.tail == 0);
    assert(tty.raw_buf.count == 1);

    assert(tty_buf_get(&tty.raw_buf, &c) == 1);
    assert(c == 'B');
    assert(tty.raw_buf.tail == 1);
    assert(tty.raw_buf.count == 0);
}

static void test_tty_write_buffer_queues_and_drains_in_order(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty tty;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.driver = &driver;
    tty.winsize.ws_col = 80;

    tty_output_locked('A', &tty);
    tty_output_locked('B', &tty);
    tty_output_locked('C', &tty);

    assert(tty.write_buf.count == 3);
    assert(tty_driver_out_len == 0);

    tty_start_locked(&tty);

    assert(tty.write_buf.count == 0);
    assert(tty_driver_out_len == 3);
    assert(memcmp(tty_driver_out, "ABC", 3) == 0);
}

static void test_tty_canon_buffer_receives_cooked_line(void) {
    struct tty tty;
    uint32_t flags = 0;
    char c;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.termios.c_lflag = ICANON;
    tty.termios.c_cc[VERASE] = 127;
    tty.termios.c_cc[VKILL] = 21;
    tty.termios.c_cc[VWERASE] = 23;
    tty.termios.c_cc[VEOF] = 4;

    assert(tty_buf_put(&tty.raw_buf, 'c') == 0);
    assert(tty_buf_put(&tty.raw_buf, 'a') == 0);
    assert(tty_buf_put(&tty.raw_buf, 't') == 0);
    assert(tty_buf_put(&tty.raw_buf, '\n') == 0);
    assert(tty_buf_put(&tty.raw_buf, (char)0xFF) == 0);
    tty.delct = 1;

    assert(canon(&tty, &flags) == 0);
    assert(tty.read_buf.count == 4);
    assert(tty.raw_buf.count == 0);
    assert(tty.delct == 0);

    assert(tty_buf_get(&tty.read_buf, &c) == 1);
    assert(c == 'c');
    assert(tty_buf_get(&tty.read_buf, &c) == 1);
    assert(c == 'a');
    assert(tty_buf_get(&tty.read_buf, &c) == 1);
    assert(c == 't');
    assert(tty_buf_get(&tty.read_buf, &c) == 1);
    assert(c == '\n');
}

static void test_tty_ixoff_high_and_low_water_marks(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
        .throttle = mock_tty_throttle,
        .unthrottle = mock_tty_unthrottle,
    };
    struct tty tty;
    uint32_t flags = 0;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.driver = &driver;
    tty.termios.c_iflag = IXOFF;
    tty.termios.c_lflag = ICANON;
    tty.termios.c_cc[VSTOP] = 19;
    tty.termios.c_cc[VSTART] = 17;
    tty.termios.c_cc[VERASE] = 127;
    tty.termios.c_cc[VKILL] = 21;
    tty.termios.c_cc[VWERASE] = 23;
    tty.termios.c_cc[VEOF] = 4;

    while (tty.raw_buf.count < (TTY_BUF_SIZE * 3 / 4) - 1) {
        assert(tty_buf_put(&tty.raw_buf, 'x') == 0);
    }

    tty_flip_buffer_push(&tty, 'y');

    assert(tty.input_stopped == 1);
    assert(tty_driver_throttle_count == 1);
    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 19);

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.driver = &driver;
    tty.termios.c_iflag = IXOFF;
    tty.termios.c_lflag = ICANON;
    tty.termios.c_cc[VSTOP] = 19;
    tty.termios.c_cc[VSTART] = 17;
    tty.termios.c_cc[VERASE] = 127;
    tty.termios.c_cc[VKILL] = 21;
    tty.termios.c_cc[VWERASE] = 23;
    tty.termios.c_cc[VEOF] = 4;
    tty.input_stopped = 1;

    assert(tty_buf_put(&tty.raw_buf, 'o') == 0);
    assert(tty_buf_put(&tty.raw_buf, 'k') == 0);
    assert(tty_buf_put(&tty.raw_buf, '\n') == 0);
    assert(tty_buf_put(&tty.raw_buf, (char)0xFF) == 0);
    tty.delct = 1;

    assert(canon(&tty, &flags) == 0);
    assert(tty.input_stopped == 0);
    assert(tty_driver_unthrottle_count == 1);
    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 17);
}

static void test_tty_c_iflag_defaults_and_roundtrip(void) {
    struct tty *tty;
    struct tty_driver driver = {0};
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 18);
    assert(tty != NULL);
    assert((tty->termios.c_iflag & (ICRNL | IXON)) == (ICRNL | IXON));

    memset(&termios, 0, sizeof(termios));
    termios.c_iflag = IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert((out.c_iflag & (IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON)) ==
           (IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));

    tty_free(tty);
}

static void test_tty_c_oflag_defaults_and_roundtrip(void) {
    struct tty *tty;
    struct tty_driver driver = {0};
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 19);
    assert(tty != NULL);
    assert((tty->termios.c_oflag & (OPOST | ONLCR)) == (OPOST | ONLCR));

    memset(&termios, 0, sizeof(termios));
    termios.c_oflag = OPOST | ONLCR | OXTABS;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert((out.c_oflag & (OPOST | ONLCR | OXTABS)) == (OPOST | ONLCR | OXTABS));

    tty_free(tty);
}

static void test_tty_c_cflag_defaults_and_roundtrip(void) {
    struct tty *tty;
    struct tty_driver driver = {0};
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 20);
    assert(tty != NULL);
    assert((tty->termios.c_cflag & (CREAD | CSIZE | HUPCL)) == (CREAD | CS8 | HUPCL));

    memset(&termios, 0, sizeof(termios));
    termios.c_cflag = CREAD | CS7 | PARENB | CSTOPB | CRTSCTS;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert((out.c_cflag & (CREAD | CSIZE | PARENB | CSTOPB | CRTSCTS)) ==
           (CREAD | CS7 | PARENB | CSTOPB | CRTSCTS));

    tty_free(tty);
}

static void test_tty_c_lflag_defaults_and_roundtrip(void) {
    struct tty *tty;
    struct tty_driver driver = {0};
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 21);
    assert(tty != NULL);
    assert((tty->termios.c_lflag & (ISIG | ICANON | ECHO | ECHOE | ECHOK)) ==
           (ISIG | ICANON | ECHO | ECHOE | ECHOK));

    memset(&termios, 0, sizeof(termios));
    termios.c_lflag = ICANON | ECHO | ECHOE | ECHOK | ISIG | TOSTOP;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert((out.c_lflag & (ICANON | ECHO | ECHOE | ECHOK | ISIG | TOSTOP)) ==
           (ICANON | ECHO | ECHOE | ECHOK | ISIG | TOSTOP));

    tty_free(tty);
}

static void test_tty_c_cc_defaults_and_roundtrip(void) {
    struct tty *tty;
    struct tty_driver driver = {0};
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 22);
    assert(tty != NULL);
    assert(tty->termios.c_cc[VINTR] == 3);
    assert(tty->termios.c_cc[VQUIT] == 28);
    assert(tty->termios.c_cc[VERASE] == 127);
    assert(tty->termios.c_cc[VKILL] == 21);
    assert(tty->termios.c_cc[VEOF] == 4);
    assert(tty->termios.c_cc[VSTART] == 17);
    assert(tty->termios.c_cc[VSTOP] == 19);
    assert(tty->termios.c_cc[VWERASE] == 23);

    memset(&termios, 0, sizeof(termios));
    termios.c_cc[VINTR] = 1;
    termios.c_cc[VQUIT] = 2;
    termios.c_cc[VERASE] = 3;
    termios.c_cc[VKILL] = 4;
    termios.c_cc[VEOF] = 5;
    termios.c_cc[VMIN] = 6;
    termios.c_cc[VTIME] = 7;
    termios.c_cc[VSTART] = 8;
    termios.c_cc[VSTOP] = 9;
    termios.c_cc[VWERASE] = 10;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert(out.c_cc[VINTR] == 1);
    assert(out.c_cc[VQUIT] == 2);
    assert(out.c_cc[VERASE] == 3);
    assert(out.c_cc[VKILL] == 4);
    assert(out.c_cc[VEOF] == 5);
    assert(out.c_cc[VMIN] == 6);
    assert(out.c_cc[VTIME] == 7);
    assert(out.c_cc[VSTART] == 8);
    assert(out.c_cc[VSTOP] == 9);
    assert(out.c_cc[VWERASE] == 10);

    tty_free(tty);
}

static void test_tty_input_parity_checks_and_stripping(void) {
    struct tty tty;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.termios.c_iflag = ISTRIP;
    tty.termios.c_cc[VEOF] = 4;

    tty_flip_buffer_push_status(&tty, (char)0xE1, 0);
    assert(tty.raw_buf.count == 2);
    assert((unsigned char)tty.raw_buf.data[0] == 0x61);
    assert((unsigned char)tty.raw_buf.data[1] == 0xFF);
    assert(tty.delct == 1);

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.termios.c_iflag = INPCK | IGNPAR;
    tty.termios.c_cc[VEOF] = 4;

    tty_flip_buffer_push_status(&tty, 'x', TTY_INPUT_PARITY_ERROR);
    assert(tty.raw_buf.count == 0);
    assert(tty.delct == 0);

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.termios.c_iflag = INPCK | ISTRIP;
    tty.termios.c_cc[VEOF] = 4;

    tty_flip_buffer_push_status(&tty, (char)0xE2, TTY_INPUT_PARITY_ERROR);
    assert(tty.raw_buf.count == 2);
    assert((unsigned char)tty.raw_buf.data[0] == 0x62);
    assert(tty.delct == 1);
}

static process_t *init_proc(int slot, int pid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    return p;
}

static void init_session_leader(process_t *proc, struct pgrp *pgrp, struct session *sess, int pgid) {
    memset(pgrp, 0, sizeof(*pgrp));
    memset(sess, 0, sizeof(*sess));
    sess->s_sid = proc->pid;
    sess->s_leader = proc;
    pgrp->pg_id = pgid;
    pgrp->pg_session = sess;
    pgrp->pg_members = proc;
    proc->p_pgrp = pgrp;
}

static int mock_tty_open(struct tty *tty) {
    (void)tty;
    tty_driver_open_count++;
    return tty_driver_open_errno;
}

static int mock_tty_install(struct tty_driver *driver, struct tty *tty) {
    (void)driver;
    (void)tty;
    tty_driver_install_count++;
    return 0;
}

static void mock_tty_remove(struct tty_driver *driver, struct tty *tty) {
    (void)driver;
    (void)tty;
    tty_driver_remove_count++;
}

static void mock_tty_close(struct tty *tty) {
    (void)tty;
    tty_driver_close_count++;
}

static int mock_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    (void)tty;
    int room = (int)sizeof(tty_driver_out) - tty_driver_out_len;
    if (room <= 0) {
        return 0;
    }
    if (count > room) {
        count = room;
    }
    memcpy(tty_driver_out + tty_driver_out_len, buf, (size_t)count);
    tty_driver_out_len += count;
    return count;
}

static int mock_tty_write_room(struct tty *tty) {
    (void)tty;
    return (int)sizeof(tty_driver_out) - tty_driver_out_len;
}

static int mock_tty_put_char(struct tty *tty, unsigned char c) {
    return mock_tty_write(tty, &c, 1);
}

static void mock_tty_flush_chars(struct tty *tty) {
    (void)tty;
    tty_driver_flush_count++;
}

static void mock_tty_throttle(struct tty *tty) {
    (void)tty;
    tty_driver_throttle_count++;
}

static void mock_tty_unthrottle(struct tty *tty) {
    (void)tty;
    tty_driver_unthrottle_count++;
}


static void test_tty_open_close_refcounts_driver_transitions(void) {
    struct tty_driver driver = {
        .open = mock_tty_open,
        .close = mock_tty_close,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 0);
    assert(tty != NULL);

    assert(tty_open(tty) == 0);
    assert(tty->count == 1);
    assert(tty->driver_active == 1);
    assert(tty_driver_open_count == 1);
    assert(tty_driver_close_count == 0);

    assert(tty_open(tty) == 0);
    assert(tty->count == 2);
    assert(tty_driver_open_count == 1);

    tty_close(tty);
    assert(tty->count == 1);
    assert(tty->driver_active == 1);
    assert(tty_driver_close_count == 0);

    tty_close(tty);
    assert(tty->count == 0);
    assert(tty->driver_active == 0);
    assert(tty_driver_close_count == 1);

    tty_free(tty);
}

static void test_tty_driver_install_and_remove_callbacks(void) {
    struct tty_driver driver = {
        .install = mock_tty_install,
        .remove = mock_tty_remove,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 23);
    assert(tty != NULL);
    assert(tty_driver_install_count == 1);
    assert(tty_driver_remove_count == 0);

    tty_free(tty);
    assert(tty_driver_remove_count == 1);
}

static void test_tty_driver_put_char_fallback_path(void) {
    struct tty_driver driver = {
        .put_char = mock_tty_put_char,
    };
    struct tty tty;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.driver = &driver;

    tty_output_locked('Z', &tty);
    tty_start_locked(&tty);

    assert(tty.write_buf.count == 0);
    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 'Z');
}

static void test_tty_driver_flush_chars_kicks_transmission(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
        .flush_chars = mock_tty_flush_chars,
    };
    struct tty tty;

    reset_env();
    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.driver = &driver;

    tty_output_locked('Q', &tty);
    tty_start_locked(&tty);

    assert(tty_driver_flush_count == 1);
    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 'Q');
}

static void test_tty_open_failure_restores_state(void) {
    struct tty_driver driver = {
        .open = mock_tty_open,
        .close = mock_tty_close,
    };
    struct tty *tty;

    reset_env();
    tty_driver_open_errno = -5;

    tty = tty_alloc(&driver, 1);
    assert(tty != NULL);

    assert(tty_open(tty) == -5);
    assert(tty->count == 0);
    assert(tty->driver_active == 0);
    assert(tty->lifecycle_busy == 0);
    assert(tty_driver_open_count == 1);
    assert(tty_driver_close_count == 0);

    tty_free(tty);
}

static void test_tiocsctty_assigns_owner(void) {
    reset_env();

    process_t *proc = init_proc(0, 42);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    init_session_leader(proc, &pgrp, &sess, 42);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 0) == 0);
    assert(tty.session == 42);
    assert(tty.pgrp == 42);
    assert(proc->tty == &tty);
}

static void test_tiocsctty_rejects_foreign_owner_without_steal(void) {
    reset_env();

    process_t *proc = init_proc(0, 50);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    tty.session = 77;
    tty.pgrp = 77;
    init_session_leader(proc, &pgrp, &sess, 50);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 0) == -1);
    assert(tty.session == 77);
    assert(proc->tty == NULL);

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 1) == 0);
    assert(tty.session == 50);
    assert(tty.pgrp == 50);
    assert(proc->tty == &tty);
}

static void test_tiocnotty_hangsup_foreground_group(void) {
    reset_env();

    process_t *proc = init_proc(0, 60);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    init_session_leader(proc, &pgrp, &sess, 61);
    current_process = proc;
    proc->tty = &tty;
    tty.session = 60;
    tty.pgrp = 61;

    assert(tty_ioctl_kern(&tty, TIOCNOTTY, 0) == 0);
    assert(proc->tty == NULL);
    assert(tty.session == 0);
    assert(tty.pgrp == 0);
    assert(signal_count == 2);
    assert(signal_pgrp[0] == 61);
    assert(signal_sig[0] == SIGHUP);
    assert(signal_pgrp[1] == 61);
    assert(signal_sig[1] == SIGCONT);
}

static void test_tiocpgrp_roundtrip(void) {
    reset_env();

    process_t *proc = init_proc(0, 70);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;
    int value = 99;
    int out = 0;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    init_session_leader(proc, &pgrp, &sess, 70);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == 0);
    assert(tty.pgrp == 99);
    assert(tty_ioctl_kern(&tty, TIOCGPGRP, (uintptr_t)&out) == 0);
    assert(out == 99);
}

static void test_tty_erase_echo_sequence(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 2);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VERASE]);

    assert(tty_driver_out_len == 4);
    assert(tty_driver_out[0] == 'a');
    assert(tty_driver_out[1] == '');
    assert(tty_driver_out[2] == ' ');
    assert(tty_driver_out[3] == '');

    tty_free(tty);
}

static void test_tty_raw_echo_sequence(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 3);
    assert(tty != NULL);
    tty->termios.c_lflag &= ~ICANON;

    tty_flip_buffer_push(tty, 'x');

    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 'x');

    tty_free(tty);
}

static void test_tty_signal_char_echo_sequence(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 4);
    assert(tty != NULL);
    tty->pgrp = 123;

    tty_flip_buffer_push(tty, tty->termios.c_cc[VINTR]);

    assert(signal_count == 1);
    assert(signal_pgrp[0] == 123);
    assert(signal_sig[0] == SIGINT);
    assert(tty_driver_out_len == 2);
    assert(tty_driver_out[0] == '^');
    assert(tty_driver_out[1] == 'C');

    tty_free(tty);
}

static void test_tty_canonical_erase_removes_previous_char(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 5);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, 'b');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VERASE]);
    tty_flip_buffer_push(tty, '\n');

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 2);
    assert(buf[0] == 'a');
    assert(buf[1] == '\n');

    tty_free(tty);
}

static void test_tty_canonical_kill_discards_pending_line(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 6);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, 'b');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VKILL]);
    tty_flip_buffer_push(tty, 'c');
    tty_flip_buffer_push(tty, '\n');

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 2);
    assert(buf[0] == 'c');
    assert(buf[1] == '\n');

    tty_free(tty);
}

static void test_tty_canonical_word_erase_discards_last_word(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[16];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 7);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, 'b');
    tty_flip_buffer_push(tty, ' ');
    tty_flip_buffer_push(tty, 'c');
    tty_flip_buffer_push(tty, 'd');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VWERASE]);
    tty_flip_buffer_push(tty, '\n');

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 4);
    assert(buf[0] == 'a');
    assert(buf[1] == 'b');
    assert(buf[2] == ' ');
    assert(buf[3] == '\n');

    tty_free(tty);
}

static void test_tty_canonical_eof_returns_pending_data_without_marker(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 8);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VEOF]);

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 1);
    assert(buf[0] == 'a');

    tty_free(tty);
}

static void test_tty_canonical_empty_eof_returns_zero(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 9);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, tty->termios.c_cc[VEOF]);

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 0);

    tty_free(tty);
}

static void test_tty_output_newline_expands_to_crlf(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 10);
    assert(tty != NULL);

    assert(tty_write(tty, "\n", 1) == 1);
    assert(tty_driver_out_len == 2);
    assert(tty_driver_out[0] == '\r');
    assert(tty_driver_out[1] == '\n');

    tty_free(tty);
}

static void test_tty_output_tab_expands_to_spaces(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 11);
    assert(tty != NULL);
    tty->termios.c_oflag |= OXTABS;

    assert(tty_write(tty, "a\t", 2) == 2);
    assert(tty_driver_out_len == 8);
    assert(tty_driver_out[0] == 'a');
    for (int i = 1; i < 8; i++) {
        assert(tty_driver_out[i] == ' ');
    }

    tty_free(tty);
}

static void test_tty_input_icrnl_translates_cr_to_nl(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 12);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, '\r');

    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 1);
    assert(buf[0] == '\n');

    tty_free(tty);
}

static void test_tty_input_xon_xoff_controls_output_flow(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 13);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, tty->termios.c_cc[VSTOP]);
    assert(tty->stopped == 1);

    assert(tty_write(tty, "a", 1) == 1);
    assert(tty_driver_out_len == 0);

    tty_flip_buffer_push(tty, tty->termios.c_cc[VSTART]);
    assert(tty->stopped == 0);
    assert(tty_driver_out_len == 1);
    assert(tty_driver_out[0] == 'a');

    tty_free(tty);
}

static void test_tty_isig_controls_signal_generation(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    char buf[8];
    int n;

    reset_env();

    tty = tty_alloc(&driver, 14);
    assert(tty != NULL);
    tty->pgrp = 321;

    tty_flip_buffer_push(tty, tty->termios.c_cc[VINTR]);
    assert(signal_count == 1);
    assert(signal_pgrp[0] == 321);
    assert(signal_sig[0] == SIGINT);

    reset_env();

    tty = tty_alloc(&driver, 15);
    assert(tty != NULL);
    tty->termios.c_lflag &= ~ISIG;

    tty_flip_buffer_push(tty, tty->termios.c_cc[VINTR]);
    tty_flip_buffer_push(tty, '\n');

    assert(signal_count == 0);
    memset(buf, 0, sizeof(buf));
    n = tty_read(tty, buf, sizeof(buf));
    assert(n == 2);
    assert((unsigned char)buf[0] == tty->termios.c_cc[VINTR]);
    assert(buf[1] == '\n');

    tty_free(tty);
}

static void test_tty_termios_get_set_roundtrip(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    struct termios termios;
    struct termios out;

    reset_env();

    tty = tty_alloc(&driver, 16);
    assert(tty != NULL);

    memset(&termios, 0, sizeof(termios));
    termios.c_iflag = IGNCR | IXON;
    termios.c_oflag = OPOST | ONLCR;
    termios.c_cflag = CREAD | CS8 | HUPCL;
    termios.c_lflag = ISIG | ECHO;
    termios.c_cc[VINTR] = 7;
    termios.c_cc[VEOF] = 5;

    assert(tty_ioctl_kern(tty, TCSETS, (uintptr_t)&termios) == 0);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TCGETS, (uintptr_t)&out) == 0);
    assert(memcmp(&out, &termios, sizeof(termios)) == 0);

    tty_free(tty);
}

static void test_tty_winsize_get_set_roundtrip(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;
    struct winsize winsize;
    struct winsize out;

    reset_env();

    tty = tty_alloc(&driver, 17);
    assert(tty != NULL);

    memset(&winsize, 0, sizeof(winsize));
    winsize.ws_row = 42;
    winsize.ws_col = 132;
    tty->pgrp = 77;

    assert(tty_ioctl_kern(tty, TIOCSWINSZ, (uintptr_t)&winsize) == 0);
    assert(signal_count == 1);
    assert(signal_pgrp[0] == 77);
    assert(signal_sig[0] == SIGWINCH);

    memset(&out, 0, sizeof(out));
    assert(tty_ioctl_kern(tty, TIOCGWINSZ, (uintptr_t)&out) == 0);
    assert(out.ws_row == 42);
    assert(out.ws_col == 132);

    tty_free(tty);
}

static void test_tiocspgrp_checks_sigttou_for_background_group(void) {
    reset_env();

    process_t *proc = init_proc(0, 80);
    thread_t thread;
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;
    int value = 81;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
    memset(&thread, 0, sizeof(thread));
    init_session_leader(proc, &pgrp, &sess, 80);
    current_process = proc;
    current_thread = &thread;
    tty.session = 80;
    tty.pgrp = 70;

    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == -1);
    assert(tty.pgrp == 70);
    assert(last_psignal_proc == proc);
    assert(last_psignal_sig == SIGTTOU);

    proc->sig_actions[SIGTTOU - 1].sa_handler = SIG_IGN;
    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == 0);
    assert(tty.pgrp == 81);
}

int main(void) {
    test_tty_init_clears_global_slots();
    test_tty_register_device_publishes_devfs_node();
    test_tty_raw_buffer_wraps_as_circular_queue();
    test_tty_write_buffer_queues_and_drains_in_order();
    test_tty_canon_buffer_receives_cooked_line();
    test_tty_ixoff_high_and_low_water_marks();
    test_tty_c_iflag_defaults_and_roundtrip();
    test_tty_c_oflag_defaults_and_roundtrip();
    test_tty_c_cflag_defaults_and_roundtrip();
    test_tty_c_lflag_defaults_and_roundtrip();
    test_tty_c_cc_defaults_and_roundtrip();
    test_tty_input_parity_checks_and_stripping();
    test_tty_driver_install_and_remove_callbacks();
    test_tty_driver_put_char_fallback_path();
    test_tty_driver_flush_chars_kicks_transmission();
    test_tty_open_close_refcounts_driver_transitions();
    test_tty_open_failure_restores_state();
    test_tiocsctty_assigns_owner();
    test_tiocsctty_rejects_foreign_owner_without_steal();
    test_tiocnotty_hangsup_foreground_group();
    test_tiocpgrp_roundtrip();
    test_tiocspgrp_checks_sigttou_for_background_group();
    test_tty_erase_echo_sequence();
    test_tty_raw_echo_sequence();
    test_tty_signal_char_echo_sequence();
    test_tty_canonical_erase_removes_previous_char();
    test_tty_canonical_kill_discards_pending_line();
    test_tty_canonical_word_erase_discards_last_word();
    test_tty_canonical_eof_returns_pending_data_without_marker();
    test_tty_canonical_empty_eof_returns_zero();
    test_tty_output_newline_expands_to_crlf();
    test_tty_output_tab_expands_to_spaces();
    test_tty_input_icrnl_translates_cr_to_nl();
    test_tty_input_xon_xoff_controls_output_flow();
    test_tty_isig_controls_signal_generation();
    test_tty_termios_get_set_roundtrip();
    test_tty_winsize_get_set_roundtrip();
    puts("host_test_tty_jobctl: PASS");
    return 0;
}
