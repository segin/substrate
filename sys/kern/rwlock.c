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
    if (!rw->writer && !sleepq_has_waiters(rwlock_writer_chan(rw))) {
        rw->readers++;
        acquired = true;
    }
    spinlock_release(&rw->lock);

    return acquired;
}

void rw_rlock(rwlock_t *rw) {
    spinlock_acquire(&rw->lock);
    /*
     * A42: gate reader admission on the writer sleepq's actual occupancy
     * rather than a persistent waiting_writers counter.  proc_exit() removes a
     * parked writer from its sleepq (without running rw_wlock's decrement) when
     * a sibling thread exits, so a raw counter leaks permanently and blocks all
     * future readers forever.  sleepq_has_waiters() reflects reality and heals
     * automatically when a parked writer is destroyed.
     */
    while (rw->writer || sleepq_has_waiters(rwlock_writer_chan(rw))) {
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
    if (rw->readers == 0 && sleepq_has_waiters(rwlock_writer_chan(rw))) {
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

    /*
     * A42: no separate waiting_writers counter.  Being parked on the writer
     * sleepq IS the record that a writer is waiting; if this thread is killed
     * while parked, proc_exit() pulls it off the sleepq and readers are no
     * longer blocked (see rw_rlock).
     */
    while (rw->writer || rw->readers != 0) {
        sleepq_add(rwlock_writer_chan(rw), me);
        spinlock_release(&rw->lock);
        sched_yield();
        spinlock_acquire(&rw->lock);
    }

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

    if (sleepq_has_waiters(rwlock_writer_chan(rw))) {
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
