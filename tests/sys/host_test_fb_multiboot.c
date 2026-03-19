#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _KERN_CMDLINE_H
#define _KERN_CONSOLE_STUB_H
#define _SYS_FILE_H
#define _SYS_MMAN_H
#define _SYS_PROC_H
#define _VFS_H
#define _VM_MAP_H
#define _VM_OBJECT_H
#define _VM_PAGER_H
#define _PMAP_H
#define _BGA_H
#define _FB_CONSOLE_H
#define _FONT_H

#include "../../sys/arch/x86-common/multiboot.h"
#include "../../sys/include/sys/fb.h"
#include "../../sys/drivers/video/fb.h"

typedef long off_t;

#define FS_CHARDEVICE 1
#define MAP_FIXED 0x10
#define VM_OBJ_TYPE_DEVICE 1
#define VM_PROT_READ 0x1
#define VM_PROT_WRITE 0x2
#define VM_PROT_EXEC 0x4
#define VM_PROT_USER 0x8
#define VM_INHERIT_NONE 0

typedef struct vm_map {
    int unused;
} vm_map_t;
typedef struct vm_object {
    void *pager;
} vm_object_t;
typedef struct fs_node {
    char name[32];
    uint32_t flags;
    int (*ioctl)(struct fs_node *, uint32_t, void *);
    void *(*mmap)(struct fs_node *, void *, size_t, int, int, off_t);
} fs_node_t;
typedef struct process {
    vm_map_t *vm_map;
} process_t;

process_t *current_process;
int hw_text_active;

static uint32_t fake_fb[64];
static uint64_t last_ioremap_phys;
static size_t last_ioremap_len;

int cmdline_get(const char *key, char *buf, size_t buflen) {
    (void)key;
    (void)buf;
    (void)buflen;
    return -1;
}

void kprint(const char *str) {
    (void)str;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size != 0) {
        size_t copy = len >= size ? size - 1 : len;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

int devfs_register_device(fs_node_t *node) {
    (void)node;
    return 0;
}

int fb_console_init(void) {
    return 0;
}

int video_ask_mode(fb_info_t *info) {
    (void)info;
    return 0;
}

void bga_install(void) {
}

void vga_install(void) {
}

void *ioremap(uint64_t phys_addr, size_t size) {
    last_ioremap_phys = phys_addr;
    last_ioremap_len = size;
    return fake_fb;
}

void iounmap(void *addr) {
    (void)addr;
}

vm_object_t *vm_object_allocate(int type, size_t size) {
    (void)type;
    (void)size;
    return NULL;
}

void vm_object_deallocate(vm_object_t *obj) {
    (void)obj;
}

void *vm_pager_allocate(int type, void *handle, size_t size, uint32_t prot, int flags) {
    (void)type;
    (void)handle;
    (void)size;
    (void)prot;
    (void)flags;
    return NULL;
}

int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t len) {
    (void)map;
    (void)addr;
    (void)len;
    return -1;
}

int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end) {
    (void)map;
    (void)start;
    (void)end;
    return -1;
}

int vm_map_insert(vm_map_t *map, vm_object_t *obj, uintptr_t offset,
    uintptr_t start, uintptr_t end, uint32_t cur_prot, uint32_t max_prot,
    int inherit) {
    (void)map;
    (void)obj;
    (void)offset;
    (void)start;
    (void)end;
    (void)cur_prot;
    (void)max_prot;
    (void)inherit;
    return -1;
}

#include "../../sys/drivers/video/fb.c"

static void reset_multiboot_state(void) {
    memset(&fb, 0, sizeof(fb));
    saved_mbi = NULL;
    saved_mbi_fb_addr = NULL;
    saved_mbi_fb_len = 0;
    last_ioremap_phys = 0;
    last_ioremap_len = 0;
}

static void test_mb_init_maps_framebuffer_and_layout(void) {
    multiboot_info_t mbi;
    fb_info_t info;

    reset_multiboot_state();
    memset(&mbi, 0, sizeof(mbi));
    memset(&info, 0, sizeof(info));
    mbi.flags = MULTIBOOT_INFO_FRAMEBUFFER_INFO;
    mbi.framebuffer_addr = 0xE0000000u;
    mbi.framebuffer_pitch = 4096;
    mbi.framebuffer_width = 1024;
    mbi.framebuffer_height = 768;
    mbi.framebuffer_bpp = 32;
    mbi.framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
    mbi.framebuffer_red_field_position = 16;
    mbi.framebuffer_red_mask_size = 8;
    mbi.framebuffer_green_field_position = 8;
    mbi.framebuffer_green_mask_size = 8;
    mbi.framebuffer_blue_field_position = 0;
    mbi.framebuffer_blue_mask_size = 8;

    saved_mbi = &mbi;
    assert(mb_init(&info) == 0);
    assert(last_ioremap_phys == 0xE0000000u);
    assert(last_ioremap_len == (size_t)4096 * 768);
    assert(info.addr == fake_fb);
    assert(info.width == 1024);
    assert(info.height == 768);
    assert(info.pitch == 4096);
    assert(info.bpp == 32);
    assert(info.red_offset == 16);
    assert(info.green_offset == 8);
    assert(info.blue_offset == 0);

    fb = info;
    assert(fb_get_raw_pixel(0x00112233, 32) == 0x00112233);
}

static void test_fb_format_conversion_routines(void) {
    memset(&fb, 0, sizeof(fb));

    fb.bpp = 32;
    assert(fb_get_raw_pixel(0x00112233, 32) == 0x00112233);

    memset(&fb, 0, sizeof(fb));
    fb.bpp = 24;
    assert(fb_get_raw_pixel(0x00ABCDEF, 24) == 0x00ABCDEF);

    memset(&fb, 0, sizeof(fb));
    fb.bpp = 16;
    assert(fb_get_raw_pixel(0x00FF0000, 16) == 0xF800);
    assert(fb_get_raw_pixel(0x0000FF00, 16) == 0x07E0);
    assert(fb_get_raw_pixel(0x000000FF, 16) == 0x001F);

    memset(&fb, 0, sizeof(fb));
    fb.bpp = 15;
    assert(fb_get_raw_pixel(0x00FF0000, 15) == 0x7C00);
    assert(fb_get_raw_pixel(0x0000FF00, 15) == 0x03E0);
    assert(fb_get_raw_pixel(0x000000FF, 15) == 0x001F);

    assert(fb_get_raw_pixel(0x00FFFFFF, 8) == 0x0F);
    assert(fb_get_raw_pixel(0x00000000, 8) == 0x00);
}

static void test_mb_init_rejects_text_mode(void) {
    multiboot_info_t mbi;
    fb_info_t info;

    reset_multiboot_state();
    memset(&mbi, 0, sizeof(mbi));
    memset(&info, 0, sizeof(info));
    mbi.flags = MULTIBOOT_INFO_FRAMEBUFFER_INFO;
    mbi.framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;

    saved_mbi = &mbi;
    assert(mb_init(&info) != 0);
    assert(last_ioremap_len == 0);
}

int main(void) {
    test_mb_init_maps_framebuffer_and_layout();
    test_fb_format_conversion_routines();
    test_mb_init_rejects_text_mode();
    puts("host_test_fb_multiboot: PASS");
    return 0;
}
