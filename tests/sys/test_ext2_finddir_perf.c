#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#define ITERATIONS 100000
#define DIR_ENTRIES 1000

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} ext2_dirent_t;

// Mock function that looks like the loop in ext2_finddir before optimization
void baseline(ext2_dirent_t *entries, int count, const char *name) {
    volatile int found_count = 0;
    for (int j = 0; j < ITERATIONS; j++) {
        for (int i = 0; i < count; i++) {
            ext2_dirent_t *de = &entries[i];
            if (de->inode != 0 && de->name_len > 0) {
                if (de->name_len == strlen(name) &&
                    strncmp(de->name, name, de->name_len) == 0) {
                    found_count++;
                }
            }
        }
    }
}

// Mock function that looks like the loop in ext2_finddir after optimization
void optimized(ext2_dirent_t *entries, int count, const char *name) {
    volatile int found_count = 0;
    size_t name_len = strlen(name);
    for (int j = 0; j < ITERATIONS; j++) {
        for (int i = 0; i < count; i++) {
            ext2_dirent_t *de = &entries[i];
            if (de->inode != 0 && de->name_len > 0) {
                if (de->name_len == name_len &&
                    strncmp(de->name, name, de->name_len) == 0) {
                    found_count++;
                }
            }
        }
    }
}

int main() {
    const char *name = "a_very_long_file_name_to_make_strlen_work_harder.txt";
    int count = DIR_ENTRIES;
    ext2_dirent_t *entries = malloc(sizeof(ext2_dirent_t) * count);

    for (int i = 0; i < count; i++) {
        entries[i].inode = 1;
        entries[i].name_len = (uint8_t)strlen("other_file");
        strcpy(entries[i].name, "other_file");
    }

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    baseline(entries, count, name);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double baseline_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Baseline (strlen in loop): %f seconds\n", baseline_time);

    clock_gettime(CLOCK_MONOTONIC, &start);
    optimized(entries, count, name);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double optimized_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Optimized (strlen once): %f seconds\n", optimized_time);

    printf("Improvement: %.2f%%\n", (baseline_time - optimized_time) / baseline_time * 100);

    free(entries);
    return 0;
}
