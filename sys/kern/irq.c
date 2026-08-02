#include <string.h>

#include <arch/i386/intr.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>

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

/* Actions unlinked by free_irq().  Never freed; see the note there. */
static irq_action_t *irq_retired;

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
        strlcpy(action->name, name, sizeof(action->name));
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
            /*
             * [USB-HW-03] RETIRE the action instead of freeing it.
             *
             * irq_dispatch() walks this chain with no lock at all (see the
             * comment there for why it must not take a plain spinlock), so a
             * CPU can be sitting on `curr` right now -- APs run sti and take
             * interrupts, they just never schedule.  kfree()ing it here hands
             * that CPU a freed node: it reads curr->next and calls
             * curr->handler out of memory the allocator has already recycled.
             * If the block has been zeroed that is a call to 0 with IF=0 in
             * supervisor mode.
             *
             * Retiring leaks sizeof(irq_action_t) per free_irq(), and
             * free_irq() is called from exactly two places (AHCI teardown and
             * the floppy probe's failure path), so the bound is a few dozen
             * bytes for the life of the system.  That is the cheapest correct
             * answer until there is real RCU to defer the free against.
             */
            curr->handler = NULL;
            curr->next = irq_retired;
            irq_retired = curr;
            spinlock_release(&irq_lock);
            intr_restore(flags_saved);
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

    /* No irq_lock here, deliberately.
     *
     * Acquiring it would deadlock: a handler that re-enables IF and takes a
     * nested IRQ on the same CPU re-enters irq_lock while it is still held.
     * That is a bug we actually hit (telnet's NIC path was the trigger) and
     * it tripped the deadlock detector.  So the read side has to stay
     * lockless.
     *
     * [USB-HW-03] This used to say "Substrate is UP today", and made
     * correctness rest on that.  It is NOT true: smp_ap_entry() runs sti and
     * parks in hlt, so every AP accepts interrupts — they simply never
     * schedule.  irq_dispatch() therefore runs concurrently on several CPUs
     * on any multi-core machine, which is the one condition QEMU was not
     * reproducing.  Safety now comes from the write side instead: free_irq()
     * retires actions rather than freeing them, so a node this walk is
     * holding can never become freed memory, and a retired node is caught by
     * the NULL-handler check below. */
    curr = irq_lines[irq];
    while (curr != NULL) {
        irq_handler_t fn = curr->handler;

        /* [USB-HW-03] A retired action has a NULL handler (free_irq clears it
         * before unlinking).  Calling through it would be a jump to 0 with
         * IF=0 in supervisor mode -- eip=0, CR2=0, err=0 -- which is precisely
         * the fault signature reported from real hardware.  Stop rather than
         * vector through it. */
        if (fn == NULL)
            break;

        handled |= fn(irq, curr->dev_id, frame);
        curr = curr->next;
    }
    return handled;
}

int irq_alloc_vector(void) {
    int i;
    uint32_t flags_saved = intr_disable();

    spinlock_acquire(&irq_lock);
    for (i = 0; i < IRQ_VECTOR_COUNT; i++) {
        /* 0x80 falls inside [IRQ_VECTOR_FIRST, IRQ_VECTOR_LAST] but is the
         * INT 0x80 syscall gate — never hand it out as a device vector. */
        if (IRQ_VECTOR_FIRST + i == 0x80) {
            continue;
        }
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
