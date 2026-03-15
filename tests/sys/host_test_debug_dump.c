#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <sys/proc.h>
#include <exec/perso/personality.h>

static char outbuf[4096];
static size_t outlen;
static thread_t *iter_threads[8];
static size_t iter_thread_count;

thread_t *current_thread;
process_t *current_process;

void kprint(const char *str) {
    size_t len = strlen(str);
    if (outlen + len >= sizeof(outbuf)) {
        len = sizeof(outbuf) - outlen - 1;
    }
    memcpy(outbuf + outlen, str, len);
    outlen += len;
    outbuf[outlen] = '\0';
}

const char *perso_name(int id) {
    switch (id) {
        case PERS_NATIVE:
            return "Substrate";
        case PERS_LINUX:
            return "Linux";
        case PERS_ELKS:
            return "ELKS";
        default:
            return "Unknown";
    }
}

void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg) {
    size_t i;

    for (i = 0; i < iter_thread_count; ++i) {
        callback(iter_threads[i], arg);
    }
}

#include "../../sys/kern/debug.c"

static void reset_state(void) {
    memset(outbuf, 0, sizeof(outbuf));
    outlen = 0;
    memset(iter_threads, 0, sizeof(iter_threads));
    iter_thread_count = 0;
    current_thread = NULL;
    current_process = NULL;
}

static void test_debug_dump_reports_bitness_column(void) {
    process_t kernel_proc;
    process_t linux_proc;
    process_t elks_proc;
    thread_t kernel_thread;
    thread_t linux_thread;
    thread_t elks_thread;

    reset_state();

    memset(&kernel_proc, 0, sizeof(kernel_proc));
    memset(&linux_proc, 0, sizeof(linux_proc));
    memset(&elks_proc, 0, sizeof(elks_proc));
    memset(&kernel_thread, 0, sizeof(kernel_thread));
    memset(&linux_thread, 0, sizeof(linux_thread));
    memset(&elks_thread, 0, sizeof(elks_thread));

    kernel_proc.pid = 0;
    kernel_proc.is_kernel_task = 1;
    kernel_proc.bitness = 32;
    strcpy(kernel_proc.comm, "swapper");

    linux_proc.pid = 1;
    linux_proc.perso_id = PERS_LINUX;
    linux_proc.bitness = 32;
    strcpy(linux_proc.comm, "sh");

    elks_proc.pid = 2;
    elks_proc.perso_id = PERS_ELKS;
    elks_proc.bitness = 16;
    strcpy(elks_proc.comm, "ps");

    kernel_thread.tid = 0;
    kernel_thread.state = THREAD_READY;
    kernel_thread.proc = &kernel_proc;

    linux_thread.tid = 1;
    linux_thread.state = THREAD_RUNNING;
    linux_thread.proc = &linux_proc;
    linux_thread.wait_reason = "poll";

    elks_thread.tid = 2;
    elks_thread.state = THREAD_BLOCKED;
    elks_thread.proc = &elks_proc;
    elks_thread.wait_reason = "sleep";

    current_thread = &linux_thread;
    current_process = &linux_proc;

    iter_threads[iter_thread_count++] = &kernel_thread;
    iter_threads[iter_thread_count++] = &linux_thread;
    iter_threads[iter_thread_count++] = &elks_thread;

    debug_dump_processes();

    assert(strstr(outbuf, "PERSO     | BITS | WAIT REASON") != NULL);
    assert(strstr(outbuf, "Current Thread: TID=1") != NULL);
    assert(strstr(outbuf, "Current Process: PID=1 (sh)") != NULL);
    assert(strstr(outbuf, "(swapper)") != NULL);
    assert(strstr(outbuf, "(kernel)  | 32") != NULL);
    assert(strstr(outbuf, "Linux     | 32") != NULL);
    assert(strstr(outbuf, "ELKS      | 16") != NULL);
}

int main(void) {
    test_debug_dump_reports_bitness_column();
    puts("host_test_debug_dump: PASS");
    return 0;
}
