/*
 * test_mouse — interactive mouse event viewer.
 *
 * Reads /dev/input/event0, decodes mouse events (REL_X / REL_Y / wheel
 * deltas and BTN_LEFT / RIGHT / MIDDLE / SIDE / EXTRA button presses
 * and releases), and redraws a live status block over ANSI escape
 * codes.  Useful for verifying the PS/2 and USB HID mouse drivers
 * and for chasing protocol-decode bugs in the input subsystem.
 *
 * Ctrl-C to exit.
 *
 * Build: make -C tests/lib/input        (host, just a sanity build)
 *        make -C tests/lib/input \
 *             CROSS=/opt/substrate/i386-unknown-substrate/bin/i386-unknown-substrate-
 *
 * Substrate's input_event_t and Linux's struct input_event have
 * different layouts (substrate uses u64 sec/usec, Linux uses
 * timeval).  This program defines its own decoder against substrate's
 * layout from <sys/input.h>; building host-side is just a compile
 * check, the binary doesn't run usefully under Linux.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Self-contained input event definitions matching substrate's
 * <sys/input.h>.  Repeated here so the program builds against host
 * glibc for a syntax-check pass (Linux's struct input_event has a
 * different layout — same byte order is not promised under glibc). */
#define EV_SYN     0x00
#define EV_KEY     0x01
#define EV_REL     0x02

#define REL_X      0x00
#define REL_Y      0x01
#define REL_WHEEL  0x08

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE   0x113
#define BTN_EXTRA  0x114

typedef struct input_event {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_t;

#define EVENT_PATH "/dev/input/event0"

/* ANSI sequences */
#define ANSI_CLEAR   "\033[2J"
#define ANSI_HOME    "\033[H"
#define ANSI_HIDE    "\033[?25l"
#define ANSI_SHOW    "\033[?25h"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_RESET   "\033[0m"
#define ANSI_GOTO(r, c) "\033[" #r ";" #c "H"

struct mouse_state {
    long   abs_x;       /* virtual: sum of REL_X deltas */
    long   abs_y;       /* virtual: sum of REL_Y deltas */
    int    last_dx;     /* most recent delta (for display) */
    int    last_dy;
    int    wheel;       /* accumulated REL_WHEEL */
    int    btn_left;
    int    btn_right;
    int    btn_middle;
    int    btn_side;
    int    btn_extra;
    unsigned long events_seen;
    unsigned long syn_seen;
};

static volatile sig_atomic_t stop_flag = 0;

static void on_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

static const char *btn_name(int set, const char *label) {
    return set ? label : "   ";
}

static void redraw(const struct mouse_state *s) {
    /* Move cursor home + write the status block.  Don't clear-screen
     * each frame: ANSI_HOME + EOL-erase per line keeps flicker low. */
    printf(ANSI_HOME);
    printf("substrate mouse test — Ctrl-C to exit\n\n");

    printf("  position (cumulative): " ANSI_BOLD "x=%-8ld y=%-8ld" ANSI_RESET
           "\033[K\n", s->abs_x, s->abs_y);
    printf("  last delta:            dx=%-6d dy=%-6d\033[K\n",
           s->last_dx, s->last_dy);
    printf("  wheel ticks:           %d\033[K\n", s->wheel);
    printf("\n");

    printf("  buttons: [%s] [%s] [%s] [%s] [%s]\033[K\n",
           btn_name(s->btn_left,   "LFT"),
           btn_name(s->btn_middle, "MID"),
           btn_name(s->btn_right,  "RGT"),
           btn_name(s->btn_side,   "SD4"),
           btn_name(s->btn_extra,  "FW5"));
    printf("\n");

    printf(ANSI_DIM "  events read: %lu (sync frames: %lu)" ANSI_RESET
           "\033[K\n", s->events_seen, s->syn_seen);
    fflush(stdout);
}

static void apply_event(struct mouse_state *s, const input_event_t *ev) {
    s->events_seen++;
    switch (ev->type) {
    case EV_REL:
        switch (ev->code) {
        case REL_X:
            s->abs_x += ev->value;
            s->last_dx = ev->value;
            break;
        case REL_Y:
            s->abs_y += ev->value;
            s->last_dy = ev->value;
            break;
        case REL_WHEEL:
            s->wheel += ev->value;
            break;
        }
        break;
    case EV_KEY:
        switch (ev->code) {
        case BTN_LEFT:   s->btn_left   = ev->value ? 1 : 0; break;
        case BTN_RIGHT:  s->btn_right  = ev->value ? 1 : 0; break;
        case BTN_MIDDLE: s->btn_middle = ev->value ? 1 : 0; break;
        case BTN_SIDE:   s->btn_side   = ev->value ? 1 : 0; break;
        case BTN_EXTRA:  s->btn_extra  = ev->value ? 1 : 0; break;
        }
        break;
    case EV_SYN:
        s->syn_seen++;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : EVENT_PATH;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "test_mouse: open(%s): %s\n", path, strerror(errno));
        return 1;
    }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    struct mouse_state s = {0};
    printf(ANSI_CLEAR ANSI_HIDE);
    redraw(&s);

    input_event_t buf[32];
    while (!stop_flag) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "\ntest_mouse: read: %s\n", strerror(errno));
            break;
        }
        if (n == 0) continue;
        if ((size_t)n % sizeof(input_event_t) != 0) {
            fprintf(stderr, "\ntest_mouse: short read (%ld bytes, not a multiple "
                            "of %lu)\n", (long)n, (unsigned long)sizeof(input_event_t));
            break;
        }
        size_t count = (size_t)n / sizeof(input_event_t);
        for (size_t i = 0; i < count; i++) {
            apply_event(&s, &buf[i]);
        }
        redraw(&s);
    }

    printf(ANSI_SHOW "\n");
    close(fd);
    return 0;
}
