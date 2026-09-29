/*
 * fat_host_stubs.h — kernel services the FAT driver needs when fat.c is
 * compiled into a host unit test.
 *
 * fat.c serialises cluster allocation and its shared scratch buffers with
 * sleep mutexes and reports through kprintf(); the host tests are single-
 * threaded, so a flag is lock enough.  These used to be missing entirely,
 * so none of the host_test_fat_* programs linked.  Keeping them here, as
 * udf_host_stubs.h does for UDF, makes them track <sys/lock.h> in one place.
 *
 * Include AFTER "../../sys/fs/fat/fat.c" (for mutex_t).
 */
#ifndef FAT_HOST_STUBS_H
#define FAT_HOST_STUBS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->name = name;
}

void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }
bool mutex_is_held(mutex_t *m) { return m->locked != 0; }

int kprintf(const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

#endif /* FAT_HOST_STUBS_H */
