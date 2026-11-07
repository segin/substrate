/*
 * netdev_sub_recv() copies a frame to the reader's buffer only after it
 * has dropped the subscriber's ring lock.  It copied under the lock, with
 * interrupts off, so a page fault on the buffer -- a user page not yet
 * present -- was taken holding a spinlock.  The buffer's page starts out
 * inaccessible (host_test_netdev_recv_fault.c, built without the kernel's
 * headers, whose <signal.h> is not the host's), and the fault handler
 * notes whether a lock was held, then opens the page so the copy goes on.
 * Built -m32, as the kernel is.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/netdev.h>

int netdev_test_locks_held;

void spinlock_init(spinlock_t *lock, const char *name) { lock->locked = 0; lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; netdev_test_locks_held++; }
void spinlock_release(spinlock_t *lock) { lock->locked = 0; netdev_test_locks_held--; }

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void sched_wakeup(void *chan) { (void)chan; }
void inet_eth_input(netdev_t *dev, const uint8_t *pkt, size_t len) { (void)dev; (void)pkt; (void)len; }

#include "../../sys/net/netdev.c"

/* netdev_register()'s, not reached */
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

/* in host_test_netdev_recv_fault.c */
void *netdev_test_guarded_buffer(void);
int netdev_test_faults(int *under_lock);

int main(void)
{
    netdev_t dev;
    uint8_t frame[100];
    uint32_t ifindex = 0;
    void *buf = netdev_test_guarded_buffer();
    int under_lock = 0;
    ssize_t n;

    memset(&dev, 0, sizeof(dev));
    dev.ifindex = 3;
    for (size_t i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)i;
    }

    netdev_sub_t *sub = netdev_subscribe(0);
    assert(sub);
    netdev_rx(&dev, frame, sizeof(frame));
    n = netdev_sub_recv(sub, buf, sizeof(frame), &ifindex);
    assert(n == (ssize_t)sizeof(frame));
    assert(ifindex == 3);
    assert(memcmp(buf, frame, sizeof(frame)) == 0);
    assert(netdev_test_faults(&under_lock) >= 1);
    if (under_lock) {
        printf("FAIL: the reader's buffer faulted with the ring lock held\n");
        return 1;
    }
    assert(netdev_test_locks_held == 0);
    netdev_unsubscribe(sub);
    printf("PASS: host_test_netdev_recv\n");
    return 0;
}
