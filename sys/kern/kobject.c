#include <sys/kobject.h>
#include <sys/lock.h>
#include <string.h>
#include <stdio.h>

#define KOBJECT_EVENT_COUNT 64
#define KOBJECT_EVENT_SIZE  128

static char kobject_events[KOBJECT_EVENT_COUNT][KOBJECT_EVENT_SIZE];
static size_t kobject_event_head;
static size_t kobject_event_count;
static spinlock_t kobject_event_lock = SPINLOCK_INIT("kobject_events");

void kobject_init(struct kobject *kobj, const char *name) {
    memset(kobj, 0, sizeof(struct kobject));
    strncpy(kobj->name, name, 31);
    kobj->refcount = 1;
}

void kset_init(struct kset *kset, const char *name) {
    kobject_init(&kset->kobj, name);
    kset->list = NULL;
    kset->count = 0;
}

struct kobject *kobject_get(struct kobject *kobj) {
    if (kobj) {
        __sync_fetch_and_add(&kobj->refcount, 1);
    }
    return kobj;
}

void kobject_put(struct kobject *kobj) {
    if (kobj) {
        if (__sync_sub_and_fetch(&kobj->refcount, 1) == 0) {
            if (kobj->release) {
                kobj->release(kobj);
            }
        }
    }
}

void kobject_uevent(const char *action, const char *subsystem, const char *name) {
    size_t idx;

    spinlock_acquire(&kobject_event_lock);
    idx = (kobject_event_head + kobject_event_count) % KOBJECT_EVENT_COUNT;
    snprintf(kobject_events[idx], sizeof(kobject_events[idx]),
             "%s %s %s\n",
             action ? action : "change",
             subsystem ? subsystem : "device",
             name ? name : "(unnamed)");
    if (kobject_event_count == KOBJECT_EVENT_COUNT) {
        kobject_event_head = (kobject_event_head + 1) % KOBJECT_EVENT_COUNT;
    } else {
        kobject_event_count++;
    }
    spinlock_release(&kobject_event_lock);
}

size_t kobject_uevent_dump(char *buf, size_t size) {
    size_t off = 0;
    size_t i;

    if (buf == NULL || size == 0) {
        return 0;
    }

    spinlock_acquire(&kobject_event_lock);
    for (i = 0; i < kobject_event_count; i++) {
        size_t idx = (kobject_event_head + i) % KOBJECT_EVENT_COUNT;
        int ret = snprintf(off < size ? buf + off : NULL,
                           off < size ? size - off : 0,
                           "%s",
                           kobject_events[idx]);
        if (ret > 0) {
            off += (size_t)ret;
        }
    }
    spinlock_release(&kobject_event_lock);

    return off;
}
