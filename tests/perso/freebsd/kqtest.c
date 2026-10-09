/*
 * kqtest.c - kqueue(2) and kevent(2), as FreeBSD has them.
 *
 * Built natively on FreeBSD/i386 and run there first, then as the same
 * binary under substrate's FreeBSD personality (README.md): every line
 * must read "ok" in both places.
 *
 *     cc -o kqtest kqtest.c -lpthread
 *
 * Covers the filters the personality has -- EVFILT_READ, EVFILT_WRITE,
 * EVFILT_USER, EVFILT_TIMER -- and the flags that decide what happens to
 * a registration once it has been reported (EV_ONESHOT, EV_DISPATCH,
 * EV_CLEAR on a user event, EV_ENABLE/EV_DISABLE), the error reporting
 * of kevent() (EV_RECEIPT, EV_ERROR in the event list, errno when the
 * list has no room), timeouts, a wake-up from another thread, and what
 * closing a descriptor does to its registration.
 *
 * Not covered, because substrate does not do it as FreeBSD does:
 * EV_CLEAR on a descriptor, the byte count in `data`, and a queue in a
 * forked child.
 */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void check(const char *what, long got, int ok) {
    printf("  %s  %s = %ld\n", ok ? "ok  " : "FAIL", what, got);
    if (!ok) fails++;
}

static const struct timespec zero = { 0, 0 };

static int change(int kq, uintptr_t ident, int filter, int flags,
                  unsigned fflags, long data, void *udata) {
    struct kevent ev;

    EV_SET(&ev, ident, filter, flags, fflags, data, udata);
    return kevent(kq, &ev, 1, NULL, 0, NULL);
}

static int collect(int kq, struct kevent *ev, int n, int ms) {
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    return kevent(kq, NULL, 0, ev, n, &ts);
}

static long elapsed_ms(const struct timespec *t0) {
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0->tv_sec) * 1000L +
           (t1.tv_nsec - t0->tv_nsec) / 1000000L;
}

static int trigger_kq;

static void *trigger_later(void *arg) {
    (void)arg;
    usleep(100000);
    change(trigger_kq, 7, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    return NULL;
}

int main(void) {
    struct kevent ev[4], chg[2];
    struct timespec t0;
    pthread_t th;
    int kq, p[2], q[2], r;
    long ms;

    setvbuf(stdout, NULL, _IONBF, 0);

    kq = kqueue();
    check("kqueue()", kq, kq >= 0);
    r = kevent(kq, NULL, 0, ev, 4, &zero);
    check("an empty queue, no wait", r, r == 0);

    /* --- descriptors ------------------------------------------------ */
    pipe(p);
    r = change(kq, p[0], EVFILT_READ, EV_ADD, 0, 0, (void *)0x1234);
    check("add EVFILT_READ on a pipe", r, r == 0);
    r = collect(kq, ev, 4, 0);
    check("nothing to read yet", r, r == 0);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    r = collect(kq, ev, 4, 150);
    ms = elapsed_ms(&t0);
    check("a 150 ms timeout returns 0", r, r == 0);
    check("  after about that long (ms)", ms, ms >= 140 && ms < 1000);

    write(p[1], "x", 1);
    r = collect(kq, ev, 4, 1000);
    check("readable after a write", r, r == 1);
    check("  ident is the descriptor", (long)ev[0].ident,
          ev[0].ident == (uintptr_t)p[0]);
    check("  filter is EVFILT_READ", ev[0].filter,
          ev[0].filter == EVFILT_READ);
    check("  udata comes back", (long)(uintptr_t)ev[0].udata,
          ev[0].udata == (void *)0x1234);
    r = collect(kq, ev, 4, 0);
    check("level-triggered: reported again", r, r == 1);

    r = change(kq, p[0], EVFILT_READ, EV_DISABLE, 0, 0, NULL);
    check("EV_DISABLE", r, r == 0);
    r = collect(kq, ev, 4, 0);
    check("  silences it", r, r == 0);
    r = change(kq, p[0], EVFILT_READ, EV_ENABLE, 0, 0, NULL);
    r = collect(kq, ev, 4, 0);
    check("EV_ENABLE brings it back", r, r == 1);

    read(p[0], ev, 1);
    r = collect(kq, ev, 4, 0);
    check("drained: quiet", r, r == 0);

    r = change(kq, p[1], EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, NULL);
    r = collect(kq, ev, 4, 0);
    check("EVFILT_WRITE on an empty pipe", r,
          r == 1 && ev[0].filter == EVFILT_WRITE);
    r = collect(kq, ev, 4, 0);
    check("EV_ONESHOT: once only", r, r == 0);
    errno = 0;
    r = change(kq, p[1], EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    check("  and gone: EV_DELETE is ENOENT", errno,
          r == -1 && errno == ENOENT);

    /* What a registration does once reported is fixed when it is made:
     * adding it again with other flags changes nothing. */
    write(p[1], "y", 1);
    r = change(kq, p[0], EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, NULL);
    r = collect(kq, ev, 4, 0);
    r = collect(kq, ev, 4, 0);
    check("EV_ADD again does not make it EV_ONESHOT", r, r == 1);

    change(kq, p[0], EVFILT_READ, EV_DELETE, 0, 0, NULL);
    r = change(kq, p[0], EVFILT_READ, EV_ADD | EV_DISPATCH, 0, 0, NULL);
    r = collect(kq, ev, 4, 0);
    check("EV_DISPATCH reports", r, r == 1);
    r = collect(kq, ev, 4, 0);
    check("  then disables", r, r == 0);
    r = change(kq, p[0], EVFILT_READ, EV_ENABLE, 0, 0, NULL);
    r = collect(kq, ev, 4, 0);
    check("  until enabled again", r, r == 1);
    read(p[0], ev, 1);
    change(kq, p[0], EVFILT_READ, EV_DELETE, 0, 0, NULL);
    r = change(kq, p[0], EVFILT_READ, EV_ADD, 0, 0, NULL);

    close(p[1]);
    r = collect(kq, ev, 4, 1000);
    check("writer closed: EV_EOF on the reader", r == 1 ? ev[0].flags : -1,
          r == 1 && (ev[0].flags & EV_EOF));

    pipe(q);
    write(q[1], "z", 1);
    r = change(kq, q[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    close(p[0]);
    r = collect(kq, ev, 4, 0);
    check("a closed descriptor's registration is gone", r,
          r == 1 && ev[0].ident == (uintptr_t)q[0]);
    close(q[0]);
    close(q[1]);
    r = collect(kq, ev, 4, 0);
    check("  and so is the other's", r, r == 0);

    /* --- errors ----------------------------------------------------- */
    errno = 0;
    r = change(kq, 9999, EVFILT_READ, EV_ADD, 0, 0, NULL);
    check("a bad descriptor: EBADF", errno, r == -1 && errno == EBADF);
    errno = 0;
    r = change(kq, 1, EVFILT_USER, EV_ENABLE, 0, 0, NULL);
    check("no such registration: ENOENT", errno, r == -1 && errno == ENOENT);

    EV_SET(&chg[0], 9999, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&chg[1], 3, EVFILT_USER, EV_ADD | EV_RECEIPT, 0, 0, NULL);
    r = kevent(kq, chg, 2, ev, 4, &zero);
    check("errors and receipts go in the event list", r, r == 2);
    check("  the failure: EV_ERROR, data EBADF", (long)ev[0].data,
          (ev[0].flags & EV_ERROR) && ev[0].data == EBADF);
    check("  the receipt: EV_ERROR, data 0", (long)ev[1].data,
          (ev[1].flags & EV_ERROR) && ev[1].data == 0 && ev[1].ident == 3);
    change(kq, 3, EVFILT_USER, EV_DELETE, 0, 0, NULL);

    errno = 0;
    r = kevent(9999, NULL, 0, ev, 1, &zero);
    check("kevent on a bad descriptor: EBADF", errno,
          r == -1 && errno == EBADF);

    /* --- user events ------------------------------------------------ */
    r = change(kq, 7, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, (void *)0x77);
    check("add EVFILT_USER", r, r == 0);
    r = collect(kq, ev, 4, 0);
    check("not triggered: quiet", r, r == 0);
    r = change(kq, 7, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFCOPY | 0x5, 0,
               (void *)0x78);
    r = collect(kq, ev, 4, 0);
    check("NOTE_TRIGGER fires it", r, r == 1 && ev[0].filter == EVFILT_USER);
    check("  fflags carried (NOTE_FFCOPY)", ev[0].fflags,
          (ev[0].fflags & NOTE_FFLAGSMASK) == 0x5);
    check("  udata is the last change's, not the add's",
          (long)(uintptr_t)ev[0].udata, ev[0].udata == (void *)0x78);
    r = change(kq, 7, EVFILT_USER, EV_KEEPUDATA, NOTE_TRIGGER, 0, NULL);
    r = collect(kq, ev, 4, 0);
    check("  unless the change says EV_KEEPUDATA",
          (long)(uintptr_t)ev[0].udata, r == 1 && ev[0].udata == (void *)0x78);
    r = collect(kq, ev, 4, 0);
    check("EV_CLEAR: once per trigger", r, r == 0);

    trigger_kq = kq;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&th, NULL, trigger_later, NULL);
    r = collect(kq, ev, 4, 5000);
    ms = elapsed_ms(&t0);
    pthread_join(th, NULL);
    check("a trigger from another thread wakes the wait", r, r == 1);
    check("  when it happens, not at the timeout (ms)", ms,
          ms >= 80 && ms < 2000);
    change(kq, 7, EVFILT_USER, EV_DELETE, 0, 0, NULL);

    /* --- timers ----------------------------------------------------- */
    r = change(kq, 1, EVFILT_TIMER, EV_ADD, 0, 50, NULL);
    check("add a 50 ms EVFILT_TIMER", r, r == 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    r = collect(kq, ev, 4, 2000);
    ms = elapsed_ms(&t0);
    check("it fires", r, r == 1 && ev[0].filter == EVFILT_TIMER);
    check("  after about 50 ms", ms, ms >= 40 && ms < 1000);
    usleep(180000);
    r = collect(kq, ev, 4, 0);
    check("  and counts the periods missed in data", (long)ev[0].data,
          r == 1 && ev[0].data >= 2);
    change(kq, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    r = collect(kq, ev, 4, 100);
    check("deleted: quiet", r, r == 0);

    close(kq);
    errno = 0;
    r = kevent(kq, NULL, 0, ev, 1, &zero);
    check("a closed queue: EBADF", errno, r == -1 && errno == EBADF);

    printf("kqtest: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
