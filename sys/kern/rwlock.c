#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/panic.h>

static inline void *rwlock_reader_chan(rwlock_t *rw) {
    return &rw->readers;
}

static inline void *rwlock_writer_chan(rwlock_t *rw) {
    return &rw->writer;
}

void rwlock_init(rwlock_t *rw, const char *name) {
    spinlock_init(&rw->lock, "rwlock");
    rw->readers = 0;
    rw->writer = 0;
    rw->waiting_writers = 0;
    rw->owner = NULL;
    rw->name = name;
}

bool rw_try_rlock(rwlock_t *rw) {
    bool acquired = false;

    spinlock_acquire(&rw->lock);
    if (!rw->writer && rw->waiting_writers == 0) {
        rw->readers++;
        acquired = true;
    }
    spinlock_release(&rw->lock);

    return acquired;
}

void rw_rlock(rwlock_t *rw) {
    spinlock_acquire(&rw->lock);
    while (rw->writer || rw->waiting_writers != 0) {
        sleepq_add(rwlock_reader_chan(rw), current_thread);
        spinlock_release(&rw->lock);
        sched_yield();
        spinlock_acquire(&rw->lock);
    }
    rw->readers++;
    spinlock_release(&rw->lock);
}

void rw_runlock(rwlock_t *rw) {
    spinlock_acquire(&rw->lock);

    if (rw->readers == 0 || rw->writer) {
        spinlock_release(&rw->lock);
        panic("Error: rw_runlock without reader ownership");
    }

    rw->readers--;
    if (rw->readers == 0 && rw->waiting_writers != 0) {
        sleepq_wake_one(rwlock_writer_chan(rw));
    }

    spinlock_release(&rw->lock);
}

bool rw_try_wlock(rwlock_t *rw) {
    bool acquired = false;

    spinlock_acquire(&rw->lock);
    if (!rw->writer && rw->readers == 0) {
        rw->writer = 1;
        rw->owner = current_thread;
        acquired = true;
    }
    spinlock_release(&rw->lock);

    return acquired;
}

void rw_wlock(rwlock_t *rw) {
    thread_t *me = current_thread;

    spinlock_acquire(&rw->lock);

    if (rw->writer && rw->owner == me) {
        spinlock_release(&rw->lock);
        panic("Deadlock: recursive rw_wlock attempted");
    }

    rw->waiting_writers++;
    while (rw->writer || rw->readers != 0) {
        sleepq_add(rwlock_writer_chan(rw), me);
        spinlock_release(&rw->lock);
        sched_yield();
        spinlock_acquire(&rw->lock);
    }

    rw->waiting_writers--;
    rw->writer = 1;
    rw->owner = me;

    spinlock_release(&rw->lock);
}

void rw_wunlock(rwlock_t *rw) {
    spinlock_acquire(&rw->lock);

    if (!rw->writer) {
        spinlock_release(&rw->lock);
        panic("Error: rw_wunlock without writer ownership");
    }
    if (rw->owner != current_thread) {
        spinlock_release(&rw->lock);
        panic("Error: rw_wunlock by non-owner");
    }

    rw->writer = 0;
    rw->owner = NULL;

    if (rw->waiting_writers != 0) {
        sleepq_wake_one(rwlock_writer_chan(rw));
    } else {
        sleepq_wake_all(rwlock_reader_chan(rw));
    }

    spinlock_release(&rw->lock);
}

bool rw_wowned(rwlock_t *rw) {
    bool owned;

    spinlock_acquire(&rw->lock);
    owned = rw->writer && rw->owner == current_thread;
    spinlock_release(&rw->lock);

    return owned;
}
