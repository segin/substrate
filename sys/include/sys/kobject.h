#ifndef _KOBJECT_H
#define _KOBJECT_H

#include <stdint.h>
#include <stddef.h>

struct kset {
    struct kobject kobj;
    struct kobject **list;
    int count;
};

struct kobject {
    char name[32];
    struct kobject *parent;
    struct kset    *kset;
    uint32_t        refcount;
};

void kobject_init(struct kobject *kobj, const char *name);
void kset_init(struct kset *kset, const char *name);

struct kobject *kobject_get(struct kobject *kobj);
void kobject_put(struct kobject *kobj);

#endif
