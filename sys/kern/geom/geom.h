#ifndef _KERN_GEOM_H
#define _KERN_GEOM_H

#include <stdint.h>
#include <stddef.h>

// Forward decls
struct geom_disk;
struct geom_provider;
struct geom_consumer;

// Disk Provider (e.g. IDE Drive)
typedef struct geom_disk {
    const char *name;
    void *priv; // Driver private data
    
    // Abstract I/O
    int (*read)(struct geom_disk *d, uint64_t lba, size_t count, void *buf);
    int (*write)(struct geom_disk *d, uint64_t lba, size_t count, const void *buf);
    
    // Geometry
    uint64_t media_size;
    uint32_t sector_size;
} geom_disk_t;

// Partition Entry
typedef struct geom_part {
    uint64_t start; // LBA
    uint64_t size;  // Sectors
    uint8_t type;
    struct geom_part *next;
} geom_part_t;

// GEOM Class (Parser)
typedef struct geom_class {
    const char *name;
    int (*sniff)(geom_disk_t *disk, uint64_t offset, int recursion_depth, const char *prefix); 
    struct geom_class *next;
} geom_class_t;

// API
void geom_init(void);
void geom_register_disk(geom_disk_t *disk);
void geom_register_class(geom_class_t *cls);

// Called by classes to report partitions
void geom_found_part(geom_disk_t *disk, const char *name, uint64_t start, uint64_t size, const char *type_name);

// Helpers
int geom_read_sector(geom_disk_t *disk, uint64_t lba, void *buf);

#endif
