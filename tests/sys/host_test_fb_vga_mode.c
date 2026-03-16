/*
 * host_test_fb_vga_mode.c - Tests for vga= command line parsing and mode matching
 */

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
typedef uint64_t resource_size_t;

#define FS_CHARDEVICE 1
#define MAP_FIXED 0x10
#define VM_OBJ_TYPE_DEVICE 1
#define VM_PROT_READ 0x1
#define VM_PROT_WRITE 0x2
#define VM_PROT_EXEC 0x4
#define VM_PROT_USER 0x8
#define VM_INHERIT_NONE 0

typedef struct vm_map { int unused; } vm_map_t;
typedef struct vm_object { void *pager; } vm_object_t;
typedef struct fs_node {
    char name[32];
    uint32_t flags;
    int (*ioctl)(struct fs_node *, uint32_t, void *);
    void *(*mmap)(struct fs_node *, void *, size_t, int, int, off_t);
} fs_node_t;
typedef struct process { vm_map_t *vm_map; } process_t;

process_t *current_process;
int hw_text_active;

static uint32_t fake_fb[64];

/* Stub cmdline: controllable for tests */
static const char *test_vga_value = NULL;
static const char *test_video_value = NULL;

int cmdline_get(const char *key, char *buf, size_t buflen) {
    const char *val = NULL;
    if (strcmp(key, "vga") == 0) val = test_vga_value;
    else if (strcmp(key, "video") == 0) val = test_video_value;
    if (!val) return -1;
    size_t len = strlen(val);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, val, len);
    buf[len] = '\0';
    return 0;
}

void kprint(const char *str) { (void)str; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size != 0) {
        size_t copy = len >= size ? size - 1 : len;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

int devfs_register_device(fs_node_t *node) { (void)node; return 0; }
void fb_console_init(void) {}
int video_ask_mode(fb_info_t *info) { (void)info; return 0; }
void bga_install(void) {}
void vga_install(void) {}
void *ioremap(uint64_t phys_addr, size_t size) { (void)phys_addr; (void)size; return fake_fb; }
void iounmap(void *addr) { (void)addr; }
vm_object_t *vm_object_allocate(int type, size_t size) { (void)type; (void)size; return NULL; }
void vm_object_deallocate(vm_object_t *obj) { (void)obj; }
void *vm_pager_allocate(int t, void *h, size_t s, uint32_t p, int f) { (void)t; (void)h; (void)s; (void)p; (void)f; return NULL; }
int vm_map_find_space(vm_map_t *m, uintptr_t *a, size_t l) { (void)m; (void)a; (void)l; return -1; }
int vm_map_remove(vm_map_t *m, uintptr_t s, uintptr_t e) { (void)m; (void)s; (void)e; return -1; }
int vm_map_insert(vm_map_t *m, vm_object_t *o, uintptr_t off,
    uintptr_t s, uintptr_t e, uint32_t cp, uint32_t mp, int inh) {
    (void)m; (void)o; (void)off; (void)s; (void)e; (void)cp; (void)mp; (void)inh; return -1;
}

/* Include fb.c to get inline access to parse/match functions */
#include "../../sys/drivers/video/fb.c"

/* ==================== Parse Tests ==================== */

static void test_parse_full_mode(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("1024x768@32", &w, &h, &bpp) == 0);
    assert(w == 1024);
    assert(h == 768);
    assert(bpp == 32);
    printf("  PASS: parse_full_mode\n");
}

static void test_parse_no_bpp(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("640x480", &w, &h, &bpp) == 0);
    assert(w == 640);
    assert(h == 480);
    assert(bpp == 0);
    printf("  PASS: parse_no_bpp\n");
}

static void test_parse_legacy_cga(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("320x200@2", &w, &h, &bpp) == 0);
    assert(w == 320);
    assert(h == 200);
    assert(bpp == 2);
    printf("  PASS: parse_legacy_cga\n");
}

static void test_parse_ega(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("640x350@4", &w, &h, &bpp) == 0);
    assert(w == 640);
    assert(h == 350);
    assert(bpp == 4);
    printf("  PASS: parse_ega\n");
}

static void test_parse_mode13h(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("320x200@8", &w, &h, &bpp) == 0);
    assert(w == 320);
    assert(h == 200);
    assert(bpp == 8);
    printf("  PASS: parse_mode13h\n");
}

static void test_parse_invalid_no_x(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("1024768", &w, &h, &bpp) != 0);
    printf("  PASS: parse_invalid_no_x\n");
}

static void test_parse_invalid_empty(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("", &w, &h, &bpp) != 0);
    printf("  PASS: parse_invalid_empty\n");
}

static void test_parse_invalid_no_height(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("640x", &w, &h, &bpp) != 0);
    printf("  PASS: parse_invalid_no_height\n");
}

static void test_parse_invalid_trailing(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("640x480@32abc", &w, &h, &bpp) != 0);
    printf("  PASS: parse_invalid_trailing\n");
}

static void test_parse_invalid_at_no_bpp(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("640x480@", &w, &h, &bpp) != 0);
    printf("  PASS: parse_invalid_at_no_bpp\n");
}

static void test_parse_null(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode(NULL, &w, &h, &bpp) != 0);
    printf("  PASS: parse_null\n");
}

static void test_parse_uppercase_x(void) {
    uint32_t w, h, bpp;
    assert(fb_parse_vga_mode("800X600@16", &w, &h, &bpp) == 0);
    assert(w == 800);
    assert(h == 600);
    assert(bpp == 16);
    printf("  PASS: parse_uppercase_x\n");
}

/* ==================== Mode Matching Tests ==================== */

/* Fake driver with known modes */
static struct video_mode_info fake_modes[] = {
    { 640, 480, 32, 1, 0, {0} },
    { 800, 600, 32, 2, 0, {0} },
    { 1024, 768, 32, 3, 0, {0} },
    { 320, 200, 8, 4, 0, {0} },
    { 640, 350, 4, 5, 0, {0} },
    { 320, 200, 2, 6, 0, {0} },
};
#define FAKE_MODE_COUNT (int)(sizeof(fake_modes) / sizeof(fake_modes[0]))

static int fake_probe(void) { return 0; }
static int fake_init(fb_info_t *info) { (void)info; return 0; }
static int fake_list_modes(struct video_mode_info *modes, int max_count) {
    if (!modes) return FAKE_MODE_COUNT;
    int count = 0;
    for (int i = 0; i < FAKE_MODE_COUNT && count < max_count; i++)
        modes[count++] = fake_modes[i];
    return count;
}
static int fake_set_mode(int mode_id) { (void)mode_id; return 0; }

static video_driver_t fake_driver = {
    .name = "fake",
    .priority = 100,
    .probe = fake_probe,
    .init = fake_init,
    .list_modes = fake_list_modes,
    .set_mode = fake_set_mode,
    .next = NULL
};

static void reset_drivers(void) {
    video_drivers = NULL;
    fake_driver.next = NULL;
}

static void test_match_exact(void) {
    reset_drivers();
    video_register_driver(&fake_driver);
    video_driver_t *drv = NULL;
    int mode_id = -1;
    assert(fb_find_matching_mode(1024, 768, 32, &drv, &mode_id) == 0);
    assert(drv == &fake_driver);
    assert(mode_id == 3);
    printf("  PASS: match_exact\n");
}

static void test_match_any_bpp(void) {
    reset_drivers();
    video_register_driver(&fake_driver);
    video_driver_t *drv = NULL;
    int mode_id = -1;
    /* 320x200 with bpp=0 should pick highest bpp (8) */
    assert(fb_find_matching_mode(320, 200, 0, &drv, &mode_id) == 0);
    assert(drv == &fake_driver);
    assert(mode_id == 4); /* mode_id 4 = 320x200@8 */
    printf("  PASS: match_any_bpp\n");
}

static void test_match_cga(void) {
    reset_drivers();
    video_register_driver(&fake_driver);
    video_driver_t *drv = NULL;
    int mode_id = -1;
    assert(fb_find_matching_mode(320, 200, 2, &drv, &mode_id) == 0);
    assert(mode_id == 6);
    printf("  PASS: match_cga\n");
}

static void test_match_ega(void) {
    reset_drivers();
    video_register_driver(&fake_driver);
    video_driver_t *drv = NULL;
    int mode_id = -1;
    assert(fb_find_matching_mode(640, 350, 4, &drv, &mode_id) == 0);
    assert(mode_id == 5);
    printf("  PASS: match_ega\n");
}

static void test_match_not_found(void) {
    reset_drivers();
    video_register_driver(&fake_driver);
    video_driver_t *drv = NULL;
    int mode_id = -1;
    assert(fb_find_matching_mode(1920, 1080, 32, &drv, &mode_id) != 0);
    printf("  PASS: match_not_found\n");
}

/* ==================== Multi-FB Registration Tests ==================== */

static void test_fb_register_device(void) {
    fb_device_count = 0;
    memset(fb_devices, 0, sizeof(fb_devices));

    fb_info_t dev = { .width = 1024, .height = 768, .bpp = 32 };
    int idx = fb_register_device(&dev);
    assert(idx == 0);
    assert(fb_device_count == 1);
    assert(fb_devices[0].width == 1024);

    fb_info_t dev2 = { .width = 800, .height = 600, .bpp = 32 };
    idx = fb_register_device(&dev2);
    assert(idx == 1);
    assert(fb_device_count == 2);
    assert(fb_devices[1].width == 800);

    printf("  PASS: fb_register_device\n");
}

static void test_fb_register_device_max(void) {
    fb_device_count = 0;
    memset(fb_devices, 0, sizeof(fb_devices));

    fb_info_t dev = { .width = 640, .height = 480, .bpp = 32 };
    for (int i = 0; i < FB_MAX_DEVICES; i++) {
        int idx = fb_register_device(&dev);
        assert(idx == i);
    }
    assert(fb_device_count == FB_MAX_DEVICES);

    /* Should fail when full */
    int idx = fb_register_device(&dev);
    assert(idx == -1);
    assert(fb_device_count == FB_MAX_DEVICES);

    printf("  PASS: fb_register_device_max\n");
}

/* ==================== Main ==================== */

int main(void) {
    printf("host_test_fb_vga_mode:\n");

    /* Parse tests */
    test_parse_full_mode();
    test_parse_no_bpp();
    test_parse_legacy_cga();
    test_parse_ega();
    test_parse_mode13h();
    test_parse_invalid_no_x();
    test_parse_invalid_empty();
    test_parse_invalid_no_height();
    test_parse_invalid_trailing();
    test_parse_invalid_at_no_bpp();
    test_parse_null();
    test_parse_uppercase_x();

    /* Mode matching tests */
    test_match_exact();
    test_match_any_bpp();
    test_match_cga();
    test_match_ega();
    test_match_not_found();

    /* Multi-FB tests */
    test_fb_register_device();
    test_fb_register_device_max();

    printf("All %d tests passed.\n", 19);
    return 0;
}
