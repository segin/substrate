/*
 * File-scope objects of size 0: an empty structure, one wrapping it, an
 * array of them.  A uniprocessor Linux without lock debugging makes
 * spinlock_t exactly that, and cc refused every such global ("invalid
 * struct type in global lowering: tasklist_lock ... size=0").  sizeof is
 * 0, as gcc gives it, and each still has an address; the objects after
 * them are untouched.
 */
#include <stdio.h>

typedef struct { } spinlock_t;
typedef struct { spinlock_t lock; } rwlock_t;

spinlock_t tasklist_lock;
static spinlock_t sb_lock = { };
rwlock_t rw = { .lock = { } };
static struct { } empty_arr[3];
int after = 42;

int main(void) {
    printf("%d %d %d %d %d\n", (int)sizeof tasklist_lock, (int)sizeof sb_lock, (int)sizeof rw,
           (int)sizeof empty_arr, after);
    printf("%d\n", (void *)&tasklist_lock != 0 && (void *)&sb_lock != 0 && (void *)&rw != 0 &&
                       (void *)empty_arr != 0);
    return 0;
}
