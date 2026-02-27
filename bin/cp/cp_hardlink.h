#ifndef CP_HARDLINK_H
#define CP_HARDLINK_H

#include <sys/types.h>

struct cp_hardlink_entry {
    dev_t dev;
    ino_t ino;
    char *path;
    int in_use;
};

struct cp_hardlink_map {
    struct cp_hardlink_entry *entries;
    size_t cap;
    size_t count;
};

struct cp_devino_entry {
    dev_t dev;
    ino_t ino;
    int in_use;
};

struct cp_devino_set {
    struct cp_devino_entry *entries;
    size_t cap;
    size_t count;
};

int cp_hardlink_map_init(struct cp_hardlink_map *map);
void cp_hardlink_map_destroy(struct cp_hardlink_map *map);
const char *cp_hardlink_map_get(const struct cp_hardlink_map *map, dev_t dev, ino_t ino);
int cp_hardlink_map_put(struct cp_hardlink_map *map, dev_t dev, ino_t ino, const char *path);

int cp_devino_set_init(struct cp_devino_set *set);
void cp_devino_set_destroy(struct cp_devino_set *set);
int cp_devino_set_contains(const struct cp_devino_set *set, dev_t dev, ino_t ino);
int cp_devino_set_insert(struct cp_devino_set *set, dev_t dev, ino_t ino);

#endif
