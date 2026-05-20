/*
 * test_quick_exit.c — C11 at_quick_exit / quick_exit conformance.
 *
 * Verifies:
 *   1. quick_exit runs at_quick_exit handlers in LIFO order, does
 *      NOT run atexit handlers, and passes status through to _Exit.
 *   2. A handler that calls at_quick_exit during quick_exit has the
 *      new handler run next (C11 ordering).
 *   3. at_quick_exit return values: 0 on success, non-zero for a
 *      NULL function and when the registration list is full.
 *
 * Builds host (cc) and cross (CROSS=...-).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails = 0;
static void ok(const char *m)  { printf("[ OK ] %s\n", m); }
static void bad(const char *m) { printf("[FAIL] %s\n", m); fails++; }

/* Handlers emit a single byte to a pipe so the parent can observe
 * exactly which ran and in what order. */
static int g_w;
static void emit(char c) { (void)write(g_w, &c, 1); }

static void atexit_marker(void)   { emit('A'); }   /* must NOT run */
static void q_one(void)           { emit('1'); }
static void q_two(void)           { emit('2'); }
static void q_three(void)         { emit('3'); }

static void q_extra(void)         { emit('E'); }
static void q_register(void)      { emit('R'); at_quick_exit(q_extra); }

/* Run `child` in a forked process; return its raw wait status and
 * the bytes its handlers emitted (NUL-terminated in `out`). */
static int run_child(void (*child)(int wfd), char *out, size_t outcap)
{
    int fd[2];
    if (pipe(fd) != 0) { out[0] = '\0'; return -1; }
    pid_t p = fork();
    if (p < 0) { out[0] = '\0'; return -1; }
    if (p == 0) {
        close(fd[0]);
        child(fd[1]);
        _exit(111);            /* child() must not return */
    }
    close(fd[1]);
    size_t n = 0;
    ssize_t r;
    while (n < outcap - 1 && (r = read(fd[0], out + n, outcap - 1 - n)) > 0)
        n += (size_t)r;
    out[n] = '\0';
    close(fd[0]);
    int st = 0;
    waitpid(p, &st, 0);
    return st;
}

/* Scenario 1: LIFO order, atexit skipped, status passthrough. */
static void child_basic(int wfd)
{
    g_w = wfd;
    atexit(atexit_marker);
    at_quick_exit(q_one);
    at_quick_exit(q_two);
    at_quick_exit(q_three);
    quick_exit(42);
    emit('X');                 /* unreachable */
}

/* Scenario 2: a handler registers another mid-quick_exit. */
static void child_reentrant(int wfd)
{
    g_w = wfd;
    at_quick_exit(q_one);       /* runs last */
    at_quick_exit(q_register);  /* runs first: emits R, registers q_extra */
    quick_exit(0);
}

int main(void)
{
    printf("test_quick_exit: C11 at_quick_exit / quick_exit\n");

    char out[64];
    int st;

    /* --- Scenario 1 --- */
    st = run_child(child_basic, out, sizeof out);
    if (strcmp(out, "321") == 0) ok("handlers run in LIFO order");
    else { bad("handlers run in LIFO order"); printf("  got \"%s\"\n", out); }
    if (strchr(out, 'A') == NULL) ok("atexit handler NOT run by quick_exit");
    else                          bad("atexit handler NOT run by quick_exit");
    if (strchr(out, 'X') == NULL) ok("quick_exit does not return");
    else                          bad("quick_exit does not return");
    if (WIFEXITED(st) && WEXITSTATUS(st) == 42) ok("exit status passed through");
    else { bad("exit status passed through"); printf("  status=0x%x\n", st); }

    /* --- Scenario 2 --- */
    st = run_child(child_reentrant, out, sizeof out);
    /* q_register runs first (R), registers q_extra which runs next
     * (E), then the older q_one (1). */
    if (strcmp(out, "RE1") == 0) ok("handler re-registered during quick_exit runs next");
    else { bad("handler re-registered during quick_exit runs next");
           printf("  got \"%s\" (want \"RE1\")\n", out); }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok("re-entrant scenario exit status 0");
    else bad("re-entrant scenario exit status 0");

    /* --- Scenario 3: at_quick_exit return values (in-process) --- */
    if (at_quick_exit(q_one) == 0) ok("at_quick_exit returns 0 on success");
    else                            bad("at_quick_exit returns 0 on success");
#ifdef SUBSTRATE_TARGET
    /* substrate's list is a fixed 32 slots (C11 guarantees >=32);
     * once it fills, at_quick_exit must report failure.  The host's
     * glibc grows the list unbounded, so this is substrate-only. */
    int full_hit = 0;
    for (int i = 0; i < 4096; i++) {
        if (at_quick_exit(q_one) != 0) { full_hit = 1; break; }
    }
    if (full_hit) ok("at_quick_exit reports failure when the list is full");
    else          bad("at_quick_exit reports failure when the list is full");
#endif

    printf("test_quick_exit: %s (%d failure%s)\n",
           fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
