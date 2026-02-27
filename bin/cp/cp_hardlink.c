#include "cp_hardlink.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CP_HARDLINK_INIT_CAP 128u
#define CP_HARDLINK_LOAD_NUM 7u
#define CP_HARDLINK_LOAD_DEN 10u

static uint64_t cp_hash_devino(dev_t dev, ino_t ino)
{
    uint64_t x = ((uint64_t)(uint32_t)dev << 32) ^ (uint64_t)ino;

    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;

    return x;
}

static int cp_hardlink_map_grow(struct cp_hardlink_map *map)
{
    struct cp_hardlink_entry *old_entries = map->entries;
    size_t old_cap = map->cap;
    size_t new_cap = map->cap ? map->cap * 2 : CP_HARDLINK_INIT_CAP;
    struct cp_hardlink_entry *new_entries;
    size_t i;

    new_entries = (struct cp_hardlink_entry *)calloc(new_cap, sizeof(*new_entries));
    if (!new_entries) {
        return -1;
    }

    map->entries = new_entries;
    map->cap = new_cap;
    map->count = 0;

    for (i = 0; i < old_cap; ++i) {
        struct cp_hardlink_entry *e = &old_entries[i];
        size_t slot;

        if (!e->in_use) {
            continue;
        }

        slot = (size_t)(cp_hash_devino(e->dev, e->ino) % map->cap);
        while (map->entries[slot].in_use) {
            slot = (slot + 1) % map->cap;
        }

        map->entries[slot] = *e;
        map->entries[slot].in_use = 1;
        map->count++;
    }

    free(old_entries);
    return 0;
}

int cp_hardlink_map_init(struct cp_hardlink_map *map)
{
    memset(map, 0, sizeof(*map));
    return cp_hardlink_map_grow(map);
}

void cp_hardlink_map_destroy(struct cp_hardlink_map *map)
{
    size_t i;

    if (!map->entries) {
        return;
    }

    for (i = 0; i < map->cap; ++i) {
        if (map->entries[i].in_use) {
            free(map->entries[i].path);
        }
    }

    free(map->entries);
    memset(map, 0, sizeof(*map));
}

const char *cp_hardlink_map_get(const struct cp_hardlink_map *map, dev_t dev, ino_t ino)
{
    size_t slot;
    size_t n;

    if (!map->entries || map->cap == 0) {
        return NULL;
    }

    slot = (size_t)(cp_hash_devino(dev, ino) % map->cap);
    for (n = 0; n < map->cap; ++n) {
        const struct cp_hardlink_entry *e = &map->entries[slot];
        if (!e->in_use) {
            return NULL;
        }
        if (e->dev == dev && e->ino == ino) {
            return e->path;
        }
        slot = (slot + 1) % map->cap;
    }

    return NULL;
}

int cp_hardlink_map_put(struct cp_hardlink_map *map, dev_t dev, ino_t ino, const char *path)
{
    size_t slot;
    size_t n;

    if ((map->count + 1) * CP_HARDLINK_LOAD_DEN >= map->cap * CP_HARDLINK_LOAD_NUM) {
        if (cp_hardlink_map_grow(map) != 0) {
            return -1;
        }
    }

    slot = (size_t)(cp_hash_devino(dev, ino) % map->cap);
    for (n = 0; n < map->cap; ++n) {
        struct cp_hardlink_entry *e = &map->entries[slot];

        if (!e->in_use) {
            e->dev = dev;
            e->ino = ino;
            e->path = strdup(path);
            if (!e->path) {
                return -1;
            }
            e->in_use = 1;
            map->count++;
            return 0;
        }

        if (e->dev == dev && e->ino == ino) {
            return 0;
        }

        slot = (slot + 1) % map->cap;
    }

    return -1;
}

static int cp_devino_set_grow(struct cp_devino_set *set)
{
    struct cp_devino_entry *old_entries = set->entries;
    size_t old_cap = set->cap;
    size_t new_cap = set->cap ? set->cap * 2 : CP_HARDLINK_INIT_CAP;
    struct cp_devino_entry *new_entries;
    size_t i;

    new_entries = (struct cp_devino_entry *)calloc(new_cap, sizeof(*new_entries));
    if (!new_entries) {
        return -1;
    }

    set->entries = new_entries;
    set->cap = new_cap;
    set->count = 0;

    for (i = 0; i < old_cap; ++i) {
        struct cp_devino_entry *e = &old_entries[i];
        size_t slot;

        if (!e->in_use) {
            continue;
        }

        slot = (size_t)(cp_hash_devino(e->dev, e->ino) % set->cap);
        while (set->entries[slot].in_use) {
            slot = (slot + 1) % set->cap;
        }

        set->entries[slot] = *e;
        set->entries[slot].in_use = 1;
        set->count++;
    }

    free(old_entries);
    return 0;
}

int cp_devino_set_init(struct cp_devino_set *set)
{
    memset(set, 0, sizeof(*set));
    return cp_devino_set_grow(set);
}

void cp_devino_set_destroy(struct cp_devino_set *set)
{
    free(set->entries);
    memset(set, 0, sizeof(*set));
}

int cp_devino_set_contains(const struct cp_devino_set *set, dev_t dev, ino_t ino)
{
    size_t slot;
    size_t n;

    if (!set->entries || set->cap == 0) {
        return 0;
    }

    slot = (size_t)(cp_hash_devino(dev, ino) % set->cap);
    for (n = 0; n < set->cap; ++n) {
        const struct cp_devino_entry *e = &set->entries[slot];

        if (!e->in_use) {
            return 0;
        }
        if (e->dev == dev && e->ino == ino) {
            return 1;
        }
        slot = (slot + 1) % set->cap;
    }

    return 0;
}

int cp_devino_set_insert(struct cp_devino_set *set, dev_t dev, ino_t ino)
{
    size_t slot;
    size_t n;

    if ((set->count + 1) * CP_HARDLINK_LOAD_DEN >= set->cap * CP_HARDLINK_LOAD_NUM) {
        if (cp_devino_set_grow(set) != 0) {
            return -1;
        }
    }

    slot = (size_t)(cp_hash_devino(dev, ino) % set->cap);
    for (n = 0; n < set->cap; ++n) {
        struct cp_devino_entry *e = &set->entries[slot];

        if (!e->in_use) {
            e->dev = dev;
            e->ino = ino;
            e->in_use = 1;
            set->count++;
            return 0;
        }
        if (e->dev == dev && e->ino == ino) {
            return 0;
        }
        slot = (slot + 1) % set->cap;
    }

    return -1;
}
