#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <arch/i386/intr.h>

#define IRQ_LINE_COUNT 256
#define IRQ_VECTOR_COUNT (IRQ_VECTOR_LAST - IRQ_VECTOR_FIRST + 1)

typedef struct irq_action {
    irq_handler_t handler;
    void *dev_id;
    unsigned long flags;
    char name[32];
    struct irq_action *next;
} irq_action_t;

static irq_action_t *irq_lines[IRQ_LINE_COUNT];
static uint8_t irq_vector_used[IRQ_VECTOR_COUNT];
static spinlock_t irq_lock = SPINLOCK_INIT("irq");

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id) {
    irq_action_t *action;
    irq_action_t *curr;

    if (irq >= IRQ_LINE_COUNT || handler == NULL) {
        return -EINVAL;
    }

    action = kmalloc(sizeof(*action));
    if (action == NULL) {
        return -ENOMEM;
    }
    memset(action, 0, sizeof(*action));
    action->handler = handler;
    action->dev_id = dev_id;
    action->flags = flags;
    if (name != NULL) {
        strncpy(action->name, name, sizeof(action->name) - 1);
    }

    /* irq_lock is also taken from interrupt context by irq_dispatch().
     * Disable interrupts on the process-side acquire so a hardware IRQ
     * arriving mid-critical-section can't recurse into the same lock. */
    uint32_t flags_saved = intr_disable();
    spinlock_acquire(&irq_lock);
    curr = irq_lines[irq];
    while (curr != NULL) {
        if (!(curr->flags & IRQF_SHARED) || !(flags & IRQF_SHARED)) {
            spinlock_release(&irq_lock);
            intr_restore(flags_saved);
            kfree(action, sizeof(*action));
            return -EBUSY;
        }
        if (curr->dev_id == dev_id && dev_id != NULL) {
            spinlock_release(&irq_lock);
            intr_restore(flags_saved);
            kfree(action, sizeof(*action));
            return -EEXIST;
        }
        curr = curr->next;
    }

    action->next = irq_lines[irq];
    irq_lines[irq] = action;
    spinlock_release(&irq_lock);
    intr_restore(flags_saved);
    return 0;
}

void free_irq(unsigned int irq, void *dev_id) {
    irq_action_t *curr;
    irq_action_t *prev = NULL;

    if (irq >= IRQ_LINE_COUNT) {
        return;
    }

    uint32_t flags_saved = intr_disable();
    spinlock_acquire(&irq_lock);
    curr = irq_lines[irq];
    while (curr != NULL) {
        if (curr->dev_id == dev_id) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                irq_lines[irq] = curr->next;
            }
            spinlock_release(&irq_lock);
            intr_restore(flags_saved);
            kfree(curr, sizeof(*curr));
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    spinlock_release(&irq_lock);
    intr_restore(flags_saved);
}

int irq_dispatch(unsigned int irq, void *frame) {
    irq_action_t *curr;
    int handled = 0;

    if (irq >= IRQ_LINE_COUNT) {
        return 0;
    }

    /* No irq_lock here.
     *
     * irq_dispatch runs in interrupt context — the CPU has IF=0 on
     * entry, so request_irq / free_irq (which both intr_disable()
     * around their lock-held mutation of irq_lines[]) cannot run
     * concurrently on this CPU.  Substrate is UP today; a future
     * SMP port will need a real lock, but it MUST NOT be a plain
     * spinlock — it has to be either dropped before invoking the
     * handler chain (so a handler that re-enables IF and takes a
     * nested IRQ doesn't re-enter irq_lock), or replaced with a
     * read-side-lockless scheme (RCU-style).
     *
     * The historical bug we hit: acquiring irq_lock here meant any
     * handler that enabled interrupts (telnet's NIC path was the
     * trigger) and let a second IRQ fire on the same CPU deadlocked
     * on the still-held lock and tripped the deadlock detector.  */
    curr = irq_lines[irq];
    while (curr != NULL) {
        handled |= curr->handler(irq, curr->dev_id, frame);
        curr = curr->next;
    }
    return handled;
}

int irq_alloc_vector(void) {
    int i;
    uint32_t flags_saved = intr_disable();

    spinlock_acquire(&irq_lock);
    for (i = 0; i < IRQ_VECTOR_COUNT; i++) {
        if (!irq_vector_used[i]) {
            irq_vector_used[i] = 1;
            spinlock_release(&irq_lock);
            intr_restore(flags_saved);
            return IRQ_VECTOR_FIRST + i;
        }
    }
    spinlock_release(&irq_lock);
    intr_restore(flags_saved);
    return -ENOSPC;
}

void irq_free_vector(int vector) {
    int idx = vector - IRQ_VECTOR_FIRST;

    if (idx < 0 || idx >= IRQ_VECTOR_COUNT) {
        return;
    }

    uint32_t flags_saved = intr_disable();
    spinlock_acquire(&irq_lock);
    irq_vector_used[idx] = 0;
    spinlock_release(&irq_lock);
    intr_restore(flags_saved);
}
