#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <string.h>
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

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id) {
    irq_action_t *action;
    irq_action_t *curr;

    if (irq >= IRQ_LINE_COUNT || handler == NULL) {
        return -EINVAL;
    }

    spinlock_acquire(&irq_lock);
    curr = irq_lines[irq];
    while (curr != NULL) {
        if (!(curr->flags & IRQF_SHARED) || !(flags & IRQF_SHARED)) {
            spinlock_release(&irq_lock);
            return -EBUSY;
        }
        if (curr->dev_id == dev_id && dev_id != NULL) {
            spinlock_release(&irq_lock);
            return -EEXIST;
        }
        curr = curr->next;
    }
    spinlock_release(&irq_lock);

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

    spinlock_acquire(&irq_lock);
    action->next = irq_lines[irq];
    irq_lines[irq] = action;
    spinlock_release(&irq_lock);
    return 0;
}

void free_irq(unsigned int irq, void *dev_id) {
    irq_action_t *curr;
    irq_action_t *prev = NULL;

    if (irq >= IRQ_LINE_COUNT) {
        return;
    }

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
            kfree(curr, sizeof(*curr));
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    spinlock_release(&irq_lock);
}

int irq_dispatch(unsigned int irq, void *frame) {
    irq_action_t *curr;
    int handled = 0;

    if (irq >= IRQ_LINE_COUNT) {
        return 0;
    }

    spinlock_acquire(&irq_lock);
    curr = irq_lines[irq];
    while (curr != NULL) {
        irq_handler_t handler = curr->handler;
        void *dev_id = curr->dev_id;
        spinlock_release(&irq_lock);
        handled |= handler(irq, dev_id, frame);
        spinlock_acquire(&irq_lock);
        curr = curr->next;
    }
    spinlock_release(&irq_lock);
    return handled;
}

int irq_alloc_vector(void) {
    int i;

    spinlock_acquire(&irq_lock);
    for (i = 0; i < IRQ_VECTOR_COUNT; i++) {
        if (!irq_vector_used[i]) {
            irq_vector_used[i] = 1;
            spinlock_release(&irq_lock);
            return IRQ_VECTOR_FIRST + i;
        }
    }
    spinlock_release(&irq_lock);
    return -ENOSPC;
}

void irq_free_vector(int vector) {
    int idx = vector - IRQ_VECTOR_FIRST;

    if (idx < 0 || idx >= IRQ_VECTOR_COUNT) {
        return;
    }

    spinlock_acquire(&irq_lock);
    irq_vector_used[idx] = 0;
    spinlock_release(&irq_lock);
}
