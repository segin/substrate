#define HOST_TEST 1
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>
#include <sys/lock.h>
#include <sys/proc.h>

/* Mocks for kernel infrastructure that random.c expects. */

void kprint(const char *str) { (void)str; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }

typedef struct fs_node fs_node_t;
void devfs_register_device(fs_node_t *node) { (void)node; }

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
bool spinlock_try_acquire(spinlock_t *lock) {
    if (lock->locked) return false;
    lock->locked = 1;
    return true;
}
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

/* CPUID-feature stubs and basic kalloc shims for the syscall paths. */
int i386_cpu_has_cpuid(void) { return 0; }
int i386_cpu_has_rdrand(void) { return 0; }
int i386_cpu_has_rdseed(void) { return 0; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *p, size_t size) { (void)size; free(p); }

/*
 * Mock current_process so the dispatcher's privilege checks are exercisable.
 * Real kernel sets this in scheduler context; the test owns it directly.
 */
process_t *current_process;

static process_t test_proc;

/* Pull in random.c directly so we can test static helpers. */
#include <kern/random.c>

/*
 * The dispatcher uses copyin/copyout — for host tests these must be
 * pass-through copies.  random.c pulls in <sys/copy.h> which declares them;
 * we provide implementations here.
 */
int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}
int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

static void as_root(void) {
    memset(&test_proc, 0, sizeof(test_proc));
    test_proc.euid = 0;
    current_process = &test_proc;
}
static void as_user(void) {
    memset(&test_proc, 0, sizeof(test_proc));
    test_proc.euid = 1000;
    current_process = &test_proc;
}

static void test_get_set_entropy_count(void) {
    random_init();

    random_set_entropy_count(123);
    assert(random_get_entropy_count() == 123);

    /* Setting above the cap clamps to ENTROPY_POOL_SIZE * 8. */
    random_set_entropy_count(1U << 30);
    assert(random_get_entropy_count() == ENTROPY_POOL_SIZE * 8);
}

static void test_add_to_entropy_count(void) {
    random_init();

    random_set_entropy_count(100);
    random_add_to_entropy_count(50);
    assert(random_get_entropy_count() == 150);

    /* Negative delta clamps at 0, never wraps. */
    random_add_to_entropy_count(-1000);
    assert(random_get_entropy_count() == 0);

    /* Positive delta clamps at the pool ceiling. */
    random_add_to_entropy_count((int)(ENTROPY_POOL_SIZE * 8) + 999);
    assert(random_get_entropy_count() == ENTROPY_POOL_SIZE * 8);
}

static void test_zap_entropy_count(void) {
    random_init();
    random_set_entropy_count(2048);
    random_zap_entropy_count();
    assert(random_get_entropy_count() == 0);
}

static void test_clear_pool(void) {
    random_init();
    /* Spread some bytes through the pool, set a non-zero entropy count. */
    for (int i = 0; i < ENTROPY_POOL_SIZE; i++) {
        rng_state.input_pool.pool[i] = (uint8_t)(i ^ 0x5A);
    }
    rng_state.input_pool.mix_ptr = 7;
    random_set_entropy_count(512);

    random_clear_pool();

    for (int i = 0; i < ENTROPY_POOL_SIZE; i++) {
        assert(rng_state.input_pool.pool[i] == 0);
    }
    assert(rng_state.input_pool.mix_ptr == 0);
    assert(random_get_entropy_count() == 0);
}

static void test_force_reseed(void) {
    uint32_t before;

    random_init();
    /*
     * Pump the entropy count up to the threshold and call reseed; reseed
     * counter should advance and seeded should flip true.
     */
    random_set_entropy_count(ENTROPY_POOL_SIZE * 8);
    before = rng_state.reseed_count;
    random_force_reseed();
    assert(rng_state.reseed_count == before + 1);
    assert(rng_state.seeded == 1);
}

static void test_ioctl_get_entropy_count(void) {
    int observed = -1;

    random_init();
    random_set_entropy_count(777);
    as_user();

    assert(random_dev_ioctl(NULL, RNDGETENTCNT, &observed) == 0);
    assert(observed == 777);
}

static void test_ioctl_get_entropy_count_rejects_null(void) {
    random_init();
    as_user();
    assert(random_dev_ioctl(NULL, RNDGETENTCNT, NULL) == -EINVAL);
}

static void test_ioctl_priv_required_for_modifying_ops(void) {
    int delta = 10;
    struct rand_pool_info info = { .entropy_count = 0, .buf_size = 0 };

    random_init();
    as_user();

    assert(random_dev_ioctl(NULL, RNDADDTOENTCNT, &delta) == -EPERM);
    assert(random_dev_ioctl(NULL, RNDADDENTROPY, &info) == -EPERM);
    assert(random_dev_ioctl(NULL, RNDZAPENTCNT, NULL) == -EPERM);
    assert(random_dev_ioctl(NULL, RNDCLEARPOOL, NULL) == -EPERM);
    assert(random_dev_ioctl(NULL, RNDRESEEDCRNG, NULL) == -EPERM);
}

static void test_ioctl_add_to_entropy_count_as_root(void) {
    int delta = 50;

    random_init();
    random_set_entropy_count(100);
    as_root();

    assert(random_dev_ioctl(NULL, RNDADDTOENTCNT, &delta) == 0);
    assert(random_get_entropy_count() == 150);
}

static void test_ioctl_zap_entropy_count_as_root(void) {
    random_init();
    random_set_entropy_count(900);
    as_root();

    assert(random_dev_ioctl(NULL, RNDZAPENTCNT, NULL) == 0);
    assert(random_get_entropy_count() == 0);
}

static void test_ioctl_clear_pool_as_root(void) {
    random_init();
    random_set_entropy_count(900);
    rng_state.input_pool.pool[0] = 0xAB;
    rng_state.input_pool.mix_ptr = 17;
    as_root();

    assert(random_dev_ioctl(NULL, RNDCLEARPOOL, NULL) == 0);
    assert(rng_state.input_pool.pool[0] == 0);
    assert(rng_state.input_pool.mix_ptr == 0);
    assert(random_get_entropy_count() == 0);
}

static void test_ioctl_reseed_as_root(void) {
    uint32_t before;

    random_init();
    random_set_entropy_count(ENTROPY_POOL_SIZE * 8);
    as_root();

    before = rng_state.reseed_count;
    assert(random_dev_ioctl(NULL, RNDRESEEDCRNG, NULL) == 0);
    assert(rng_state.reseed_count == before + 1);
}

static void test_ioctl_add_entropy_as_root(void) {
    /* Build a pool_info immediately followed by 32 bytes of payload. */
    union {
        struct rand_pool_info info;
        uint8_t raw[sizeof(struct rand_pool_info) + 32];
    } packet;

    random_init();
    random_set_entropy_count(0);
    as_root();

    memset(&packet, 0, sizeof(packet));
    packet.info.entropy_count = 8;
    packet.info.buf_size = 32;
    for (int i = 0; i < 32; i++) {
        packet.raw[sizeof(struct rand_pool_info) + i] = (uint8_t)(i + 1);
    }

    assert(random_dev_ioctl(NULL, RNDADDENTROPY, &packet) == 0);
    /* random_harvest credits 0 bits when called from RNDADDENTROPY path,
     * the entropy_count delta (8) is added separately via add_to_entropy. */
    assert(random_get_entropy_count() == 8);
}

static void test_ioctl_add_entropy_rejects_negative_sizes(void) {
    struct rand_pool_info info = { .entropy_count = -1, .buf_size = 0 };
    struct rand_pool_info info_size = { .entropy_count = 0, .buf_size = -1 };

    random_init();
    as_root();

    assert(random_dev_ioctl(NULL, RNDADDENTROPY, &info) == -EINVAL);
    assert(random_dev_ioctl(NULL, RNDADDENTROPY, &info_size) == -EINVAL);
}

static void test_ioctl_unknown_request_returns_enotty(void) {
    random_init();
    as_root();
    assert(random_dev_ioctl(NULL, 0xDEADBEEFU, NULL) == -ENOTTY);
}

int main(void) {
    test_get_set_entropy_count();
    test_add_to_entropy_count();
    test_zap_entropy_count();
    test_clear_pool();
    test_force_reseed();
    test_ioctl_get_entropy_count();
    test_ioctl_get_entropy_count_rejects_null();
    test_ioctl_priv_required_for_modifying_ops();
    test_ioctl_add_to_entropy_count_as_root();
    test_ioctl_zap_entropy_count_as_root();
    test_ioctl_clear_pool_as_root();
    test_ioctl_reseed_as_root();
    test_ioctl_add_entropy_as_root();
    test_ioctl_add_entropy_rejects_negative_sizes();
    test_ioctl_unknown_request_returns_enotty();
    puts("host_test_random_ioctl: PASS");
    return 0;
}
