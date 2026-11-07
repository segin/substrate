/*
 * The host side of host_test_netdev_recv: a buffer on a page that faults
 * on first touch, and a SIGSEGV handler that counts the faults, notes any
 * taken while the test's spinlock count is up, and opens the page.  Built
 * without the kernel's include paths, for the host's <signal.h>.
 */
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern int netdev_test_locks_held;

static void *guarded_page;
static size_t page_size;
static volatile sig_atomic_t faults;
static volatile sig_atomic_t faults_under_lock;

static void on_fault(int sig, siginfo_t *si, void *ctx)
{
    (void)sig; (void)si; (void)ctx;
    faults++;
    if (netdev_test_locks_held > 0) {
        faults_under_lock++;
    }
    mprotect(guarded_page, page_size, PROT_READ | PROT_WRITE);
}

void *netdev_test_guarded_buffer(void)
{
    struct sigaction sa;

    page_size = (size_t)sysconf(_SC_PAGESIZE);
    guarded_page = mmap(NULL, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(guarded_page != MAP_FAILED);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    return guarded_page;
}

int netdev_test_faults(int *under_lock)
{
    *under_lock = faults_under_lock;
    return faults;
}
