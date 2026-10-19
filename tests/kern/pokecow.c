/*
 * pokecow — a ptrace POKE changes the tracee's memory and no one else's.
 *
 * pmap_copyout_other wrote straight to the physical page behind the
 * tracee's mapping.  For a page the tracee still shared with its parent
 * after fork, the parent saw the store; for a page of the program's text,
 * which every process running that file maps, all of them did.
 *
 * The child stops for its parent, which pokes a word of the child's data
 * and a word of the child's text.  The child must see both stores; the
 * parent, running the same file from the same fork, must see neither.
 *
 * Builds for the host as well, where every line must also read ok:
 *
 *      cc -o pokecow pokecow.c && ./pokecow
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define OLD_DATA 0x11111111L
#define NEW_DATA 0x22222222L

static int failed;
static volatile long data_word = OLD_DATA;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* Never called: its first word is only read and poked. */
__attribute__((noinline, aligned(16))) int victim(int x) {
    return x * 3 + 7;
}

/* victim's first word as it is now; read through a volatile pointer, or
 * the compiler reuses an earlier read of what it takes to be constant. */
static long text_now(void) {
    const volatile unsigned char *p = (const volatile unsigned char *)victim;
    long w = 0;
    for (size_t i = 0; i < sizeof(w); i++)
        ((unsigned char *)&w)[i] = p[i];
    return w;
}

/* What the parent pokes over victim's first word. */
static long poked_text(long word) {
    return word ^ 0x5A5A5A5AL;
}

int main(void) {
    long text_word;
    pid_t pid;
    int st = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    text_word = text_now();

    pid = fork();
    if (pid == 0) {
        long now;
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        raise(SIGSTOP);
        now = text_now();
        _exit((data_word == NEW_DATA ? 0 : 1) |
              (now == poked_text(text_word) ? 0 : 2));
    }
    check(pid > 0, "fork");
    if (pid <= 0) return 1;

    waitpid(pid, &st, 0);
    check(WIFSTOPPED(st), "child stopped for its tracer");
    check(ptrace(PTRACE_POKEDATA, pid, (void *)&data_word,
                 (void *)NEW_DATA) == 0, "poke the child's data");
    check(ptrace(PTRACE_POKETEXT, pid, (void *)victim,
                 (void *)poked_text(text_word)) == 0, "poke the child's text");
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, &st, 0);

    check(WIFEXITED(st) && (WEXITSTATUS(st) & 1) == 0,
          "child sees the data it was poked");
    check(WIFEXITED(st) && (WEXITSTATUS(st) & 2) == 0,
          "child sees the text it was poked");

    long now = text_now();
    check(data_word == OLD_DATA, "parent's data unchanged");
    check(now == text_word, "parent's text unchanged (same file)");
    check(victim(1) == 10, "parent's text still runs");

    printf("%s\n", failed ? "pokecow: FAIL" : "pokecow: PASS");
    return failed;
}
