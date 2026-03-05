#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#define ITERATIONS 100000
#define DIR_ENTRIES 1000

#define EXT2_DCACHE_SIZE 16

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} ext2_dirent_t;

typedef struct {
    char name[64];
    uint32_t inode_num;
} ext2_dcache_entry_t;

typedef struct {
    ext2_dcache_entry_t dcache[EXT2_DCACHE_SIZE];
    uint32_t dcache_idx;
} ext2_node_t;

// Mock function that looks like the loop in ext2_finddir without dcache
int baseline_finddir(ext2_dirent_t *entries, int count, const char *name) {
    volatile int found_count = 0;
    size_t name_len = strlen(name);
    for (int j = 0; j < ITERATIONS; j++) {
        for (int i = 0; i < count; i++) {
            ext2_dirent_t *de = &entries[i];
            if (de->inode != 0 && de->name_len > 0) {
                if (de->name_len == name_len &&
                    memcmp(de->name, name, de->name_len) == 0) {
                    found_count++;
                    break;
                }
            }
        }
    }
    return found_count;
}

// Mock function that looks like the loop in ext2_finddir with dcache
int optimized_finddir(ext2_node_t *node, ext2_dirent_t *entries, int count, const char *name) {
    volatile int found_count = 0;
    size_t name_len = strlen(name);

    for (int j = 0; j < ITERATIONS; j++) {
        int found_inode = 0;

        // 1. Check dcache
        for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
            if (node->dcache[k].inode_num != 0 &&
                strncmp(node->dcache[k].name, name, 64) == 0) {
                found_inode = node->dcache[k].inode_num;
                break;
            }
        }

        if (found_inode != 0) {
            found_count++;
            continue;
        }

        // 2. Linear search if not in dcache
        for (int i = 0; i < count; i++) {
            ext2_dirent_t *de = &entries[i];
            if (de->inode != 0 && de->name_len > 0) {
                if (de->name_len == name_len &&
                    memcmp(de->name, name, de->name_len) == 0) {

                    // Add to dcache
                    uint32_t idx = node->dcache_idx++ % EXT2_DCACHE_SIZE;
                    node->dcache[idx].inode_num = de->inode;
                    size_t copy_len = name_len < 63 ? name_len : 63;
                    memcpy(node->dcache[idx].name, name, copy_len);
                    node->dcache[idx].name[copy_len] = '\0';

                    found_count++;
                    break;
                }
            }
        }
    }
    return found_count;
}

int main() {
    const char *name = "the_target_file.txt";
    int count = DIR_ENTRIES;
    ext2_dirent_t *entries = malloc(sizeof(ext2_dirent_t) * count);

    for (int i = 0; i < count; i++) {
        entries[i].inode = 1;
        entries[i].name_len = (uint8_t)strlen("other_file");
        strcpy(entries[i].name, "other_file");
    }

    // Put target at the end
    entries[count - 1].inode = 42;
    entries[count - 1].name_len = (uint8_t)strlen(name);
    strcpy(entries[count - 1].name, name);

    ext2_node_t node;
    memset(&node, 0, sizeof(node));

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    int res_base = baseline_finddir(entries, count, name);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double baseline_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Baseline (linear search): %f seconds (found %d)\n", baseline_time, res_base);

    clock_gettime(CLOCK_MONOTONIC, &start);
    int res_opt = optimized_finddir(&node, entries, count, name);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double optimized_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Optimized (dcache): %f seconds (found %d)\n", optimized_time, res_opt);

    printf("Improvement: %.2f%%\n", (baseline_time - optimized_time) / baseline_time * 100);

    free(entries);
    return 0;
}
