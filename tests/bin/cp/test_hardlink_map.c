#include "cp_hardlink.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; \
} } while (0)

int main(void)
{
    struct cp_hardlink_map map;
    struct cp_devino_set set;

    CHECK(cp_hardlink_map_init(&map) == 0);
    CHECK(cp_hardlink_map_put(&map, 1, 2, "/tmp/a") == 0);
    CHECK(cp_hardlink_map_put(&map, 1, 3, "/tmp/b") == 0);
    CHECK(strcmp(cp_hardlink_map_get(&map, 1, 2), "/tmp/a") == 0);
    CHECK(cp_hardlink_map_get(&map, 9, 9) == NULL);
    cp_hardlink_map_destroy(&map);

    CHECK(cp_devino_set_init(&set) == 0);
    CHECK(cp_devino_set_contains(&set, 1, 2) == 0);
    CHECK(cp_devino_set_insert(&set, 1, 2) == 0);
    CHECK(cp_devino_set_contains(&set, 1, 2) == 1);
    CHECK(cp_devino_set_insert(&set, 1, 2) == 0);
    cp_devino_set_destroy(&set);

    return 0;
}
