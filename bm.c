#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

typedef struct {
    char *name;
    int type;
    int type_struct_id;
    long array_len;
    int array_ndim;
    long array_dims[4];
    long offset;
    long size;
    int is_bitfield;
    int bit_width;
    int bit_offset;
    int bit_storage_bits;
    int bit_signed;
    long bit_storage_size;
} cc_struct_member_t;

typedef struct {
    char *tag;
    int depth;
    cc_struct_member_t *members;
    size_t member_count;
    size_t member_cap;
    long size;
    long align;
    int attr_flags;
    long attr_align;
    char *attr_alias;
    int is_union;
    int has_flexible_array;
    int complete;
} cc_struct_def_t;

void benchmark_linear() {
    int iterations = 10000;
    cc_struct_def_t sd = {0};
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        cc_struct_member_t *next = (cc_struct_member_t *)realloc(sd.members, (sd.member_count + 1) * sizeof(*next));
        if (next == NULL) {
            printf("OOM\n");
            return;
        }
        sd.members = next;
        memset(&sd.members[sd.member_count], 0, sizeof(cc_struct_member_t));
        sd.member_count++;
    }
    clock_t end = clock();
    printf("Linear time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(sd.members);
}

void benchmark_geometric() {
    int iterations = 10000;
    cc_struct_def_t sd = {0};
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        if (sd.member_count == sd.member_cap) {
            size_t ncap = sd.member_cap == 0 ? 8 : sd.member_cap * 2;
            cc_struct_member_t *next = (cc_struct_member_t *)realloc(sd.members, ncap * sizeof(*next));
            if (next == NULL) {
                printf("OOM\n");
                return;
            }
            sd.members = next;
            sd.member_cap = ncap;
        }
        memset(&sd.members[sd.member_count], 0, sizeof(cc_struct_member_t));
        sd.member_count++;
    }
    clock_t end = clock();
    printf("Geometric time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(sd.members);
}

int main() {
    benchmark_linear();
    benchmark_geometric();
    return 0;
}
