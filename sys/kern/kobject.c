#include <sys/kobject.h>
#include <string.h>

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
            // TODO: Call release callback if implemented
            // kfree(kobj); if dynamic?
            // For now, this is a placeholder for proper object lifecycle.
        }
    }
}
