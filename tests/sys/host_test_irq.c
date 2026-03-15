#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/irq.h>
#include <sys/lock.h>

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void spinlock_init(spinlock_t *lock, const char *name) { lock->locked = 0; lock->cpu_id = 0; lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

#define HOST_TEST 1
#include "../../sys/kern/irq.c"

static int counter_a;
static int counter_b;

static int handler_a(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame; counter_a++; return 1;
}

static int handler_b(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame; counter_b++; return 1;
}

int main(void) {
    assert(request_irq(5, handler_a, 0, "a", (void *)1) == 0);
    assert(request_irq(5, handler_b, 0, "b", (void *)2) == -16);
    assert(irq_dispatch(5, NULL) == 1);
    assert(counter_a == 1);
    free_irq(5, (void *)1);

    counter_a = 0;
    counter_b = 0;
    assert(request_irq(7, handler_a, IRQF_SHARED, "a", (void *)1) == 0);
    assert(request_irq(7, handler_b, IRQF_SHARED, "b", (void *)2) == 0);
    assert(irq_dispatch(7, NULL) == 1);
    assert(counter_a == 1 && counter_b == 1);
    free_irq(7, (void *)1);
    free_irq(7, (void *)2);

    int v1 = irq_alloc_vector();
    int v2 = irq_alloc_vector();
    assert(v1 >= IRQ_VECTOR_FIRST && v1 <= IRQ_VECTOR_LAST);
    assert(v2 == v1 + 1);
    irq_free_vector(v1);
    irq_free_vector(v2);
    return 0;
}
