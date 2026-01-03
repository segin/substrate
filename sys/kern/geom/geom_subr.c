#include "geom.h"
#include <kern/console.h>
#include <string.h>

static geom_class_t *geom_classes = NULL;

void geom_init(void) {
    // Helper to init structures if needed
}

void geom_register_class(geom_class_t *cls) {
    if (!cls) return;
    cls->next = geom_classes;
    geom_classes = cls;
}

int geom_read_sector(geom_disk_t *disk, uint64_t lba, void *buf) {
    if (!disk || !disk->read) return -1;
    return disk->read(disk, lba, 1, buf);
}

// Recursive scan
void geom_scan(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    if (depth > 8) return; // Recursion limit

    for (geom_class_t *cls = geom_classes; cls != NULL; cls = cls->next) {
        if (cls->sniff(disk, offset, depth, prefix) == 0) {
            return; // Found a match handler
        }
    }
}

void geom_register_disk(geom_disk_t *disk) {
    if (!disk) return;
    // Start scanning from LBA 0 with disk name as prefix
    geom_scan(disk, 0, 0, disk->name);
}

void geom_found_part(geom_disk_t *disk, const char *name, uint64_t start, uint64_t size, const char *type_name) {
    (void)disk; (void)start; (void)size; (void)name; (void)type_name;
    // Stub: usually registers a new device node in DevFS or updates partition list
}
