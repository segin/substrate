#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOST_TEST 1

#include <kern/sched.h>
#include <sys/proc.h>

thread_t thread0;
thread_t *current_thread;
int num_cpus = 4;
static int mock_cpu_id;
static int last_ipi_dest;
static int last_ipi_vector;
static int ipi_send_count;
static int ipi_broadcast_count;
static int eoi_count;

int percpu_get_cpu_id(void) {
    return mock_cpu_id;
}

void lapic_send_ipi(uint8_t dest_cpu, uint8_t vector) {
    last_ipi_dest = dest_cpu;
    last_ipi_vector = vector;
    ipi_send_count++;
}

void lapic_send_ipi_all_excl_self(uint8_t vector) {
    last_ipi_vector = vector;
    ipi_broadcast_count++;
}

void lapic_send_eoi(void) {
    eoi_count++;
}

thread_t *runqueue_peek(void *rq) {
    (void)rq;
    return NULL;
}

void *sched_get_current_runqueue(void) {
    return NULL;
}

#include "../../sys/kern/sched_ipi.c"

static void reset_env(void) {
    memset(&thread0, 0, sizeof(thread0));
    current_thread = &thread0;
    current_thread->state = THREAD_RUNNING;
    mock_cpu_id = 1;
    last_ipi_dest = -1;
    last_ipi_vector = -1;
    ipi_send_count = 0;
    ipi_broadcast_count = 0;
    eoi_count = 0;
}

static void test_remote_ipi_send_and_broadcast(void) {
    reset_env();

    sched_send_preempt_ipi(2);
    assert(ipi_send_count == 1);
    assert(last_ipi_dest == 2);
    assert(last_ipi_vector == SCHED_IPI_VECTOR);

    sched_send_preempt_all();
    assert(ipi_broadcast_count == 1);
    assert(last_ipi_vector == SCHED_IPI_VECTOR);
}

static void test_local_and_remote_resched_requests(void) {
    reset_env();

    sched_resched_cpu(1);
    assert(current_thread->needs_resched == 1);
    assert(ipi_send_count == 0);

    current_thread->needs_resched = 0;
    sched_resched_cpu(3);
    assert(current_thread->needs_resched == 0);
    assert(ipi_send_count == 1);
    assert(last_ipi_dest == 3);
    assert(last_ipi_vector == SCHED_IPI_VECTOR);
}

static void test_ipi_handler_marks_thread_and_acks(void) {
    reset_env();

    sched_ipi_handler();
    assert(current_thread->needs_resched == 1);
    assert(eoi_count == 1);
    assert(sched_needs_resched() == 1);

    sched_clear_resched();
    assert(sched_needs_resched() == 0);
}

int main(void) {
    test_remote_ipi_send_and_broadcast();
    test_local_and_remote_resched_requests();
    test_ipi_handler_marks_thread_and_acks();
    puts("PASS: host_test_sched_ipi");
    return 0;
}
