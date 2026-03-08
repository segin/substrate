#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOST_TEST 1

#include <sys/proc.h>
#include <arch/i386/percpu.h>
#include <arch/i386/smp.h>

cpu_info_t cpus[MAX_CPUS];
int cpu_count = 0;
static uint32_t mock_lapic_id;

void kprint(const char *msg) { (void)msg; }
uint32_t lapic_get_id(void) { return mock_lapic_id; }

#include "../../sys/arch/i386/percpu.c"

static void reset_env(void) {
    memset(cpus, 0, sizeof(cpus));
    cpu_count = 2;
    cpus[0].lapic_id = 3;
    cpus[1].lapic_id = 7;
    mock_lapic_id = 3;

    percpu_init_cpu(0);
    percpu_init_cpu(1);
}

static void test_percpu_lookup_and_isolation(void) {
    thread_t thread0;
    thread_t thread1;

    reset_env();
    memset(&thread0, 0, sizeof(thread0));
    memset(&thread1, 0, sizeof(thread1));

    struct percpu_data *cpu0 = percpu_get_cpu(0);
    struct percpu_data *cpu1 = percpu_get_cpu(1);

    assert(cpu0 != NULL);
    assert(cpu1 != NULL);
    assert(cpu0 != cpu1);

    cpu0->current = &thread0;
    cpu0->ticks = 11;
    cpu0->runqueue_count = 2;

    cpu1->current = &thread1;
    cpu1->ticks = 29;
    cpu1->runqueue_count = 5;

    mock_lapic_id = 3;
    assert(percpu_get() == cpu0);
    assert(percpu_get_cpu_id() == 0);
    assert(THIS_CPU()->current == &thread0);
    assert(THIS_CPU()->ticks == 11);
    assert(THIS_CPU()->runqueue_count == 2);

    mock_lapic_id = 7;
    assert(percpu_get() == cpu1);
    assert(percpu_get_cpu_id() == 1);
    assert(THIS_CPU()->current == &thread1);
    assert(THIS_CPU()->ticks == 29);
    assert(THIS_CPU()->runqueue_count == 5);

    mock_lapic_id = 0xFE;
    assert(percpu_get() == cpu0);
    assert(percpu_get_cpu_id() == 0);
}

int main(void) {
    test_percpu_lookup_and_isolation();
    puts("PASS: host_test_percpu");
    return 0;
}
