#include "../sys/kobject.h"
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
