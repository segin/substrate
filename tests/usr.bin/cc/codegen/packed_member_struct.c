/*
 * `struct { ... } __attribute__((packed)) name;` declaring a member: the
 * attribute after the braces is the structure's, as it is at file scope.
 * cc laid such a structure out unpacked -- Linux's edd_device_params,
 * whose scsi path is a u16 then a u64, came out 78 bytes, not 74, and
 * struct boot_params, which the boot loader fills, 4120, not 4096.
 */
#include <stddef.h>
#include <stdio.h>

union path {
    struct {
        unsigned short id;
        unsigned long long lun;
        unsigned short reserved1;
        unsigned int reserved2;
    } __attribute__((packed)) scsi;
    struct {
        unsigned long long serial;
        unsigned long long reserved;
    } __attribute__((packed)) usb;
};

struct params {
    unsigned short length;
    union path device_path;
    struct {
        unsigned char bus;
        unsigned long long tag;
    } __attribute__((packed)) tagged;
    unsigned char checksum;
} __attribute__((packed));

/* a structure defined elsewhere keeps its own layout */
struct plain {
    unsigned char c;
    unsigned int i;
};

struct holder {
    struct plain p __attribute__((packed));
    char after;
};

int main(void) {
    printf("scsi %zu path %zu\n", sizeof(((union path *)0)->scsi), sizeof(union path));
    printf("params %zu tagged %zu at %zu checksum at %zu\n", sizeof(struct params),
           sizeof(((struct params *)0)->tagged), offsetof(struct params, tagged),
           offsetof(struct params, checksum));
    printf("plain %zu holder %zu after at %zu\n", sizeof(struct plain), sizeof(struct holder),
           offsetof(struct holder, after));
    return 0;
}
