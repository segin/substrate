#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_segment_align: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj;
    elf_segment_t *load_align_0x1000;
    elf_segment_t *tls_align_8;
    elf_segment_t *dynamic_align_default;

    obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("elf_create");

    load_align_0x1000 = elf_add_load_segment(obj, 0x5, 0x1000);
    tls_align_8 = elf_add_tls_segment(obj, 8);
    dynamic_align_default = elf_add_dynamic_segment(obj, 0);

    if (!load_align_0x1000 || !tls_align_8 || !dynamic_align_default) fail("add segments");

    /* test elf_segment_align coverage */
    if (elf_segment_align(load_align_0x1000) != 0x1000) fail("load align");
    if (elf_segment_align(tls_align_8) != 8) fail("tls align");
    /* when align is passed as 0, the default align is 1 */
    if (elf_segment_align(dynamic_align_default) != 1) fail("dynamic align default");
    if (elf_segment_align(NULL) != 0) fail("null segment align");

    elf_close(obj);
    printf("test_segment_align: ALL CHECKS PASSED\n");
    return 0;
}
