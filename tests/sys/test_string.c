#include <kern/console.h>
#include <string.h>
#include <vm/vm_kmem.h>

static int failed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        failed_tests++; \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, size, msg) do { \
    if (memcmp(a, b, size) != 0) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        failed_tests++; \
    } \
} while(0)

static void test_memcpy_basic(void) {
    char src[] = "Hello World";
    char dest[20] = {0};

    memcpy(dest, src, 12); // Include null terminator
    ASSERT_MEM_EQ(dest, src, 12, "Basic memcpy failed");
}

static void test_memcpy_small(void) {
    char src[] = "12345678";
    char dest[10];

    for (int i = 0; i <= 8; i++) {
        memset(dest, 0, sizeof(dest));
        memcpy(dest, src, i);
        if (memcmp(dest, src, i) != 0) {
            kprint("FAIL: Small memcpy size ");
            // no printf %d easily avail unless I include it, but kprint is simple
            failed_tests++;
        }
    }
}

static void test_memcpy_unaligned(void) {
    // 64 bytes buffer
    char src_buf[64];
    char dest_buf[64];

    for (int i = 0; i < 64; i++) src_buf[i] = (char)i;

    // Unaligned dest (offset 1)
    memset(dest_buf, 0, 64);
    memcpy(dest_buf + 1, src_buf, 10);
    ASSERT_MEM_EQ(dest_buf + 1, src_buf, 10, "Unaligned dest memcpy failed");

    // Unaligned src (offset 1)
    memset(dest_buf, 0, 64);
    memcpy(dest_buf, src_buf + 1, 10);
    ASSERT_MEM_EQ(dest_buf, src_buf + 1, 10, "Unaligned src memcpy failed");

    // Both unaligned
    memset(dest_buf, 0, 64);
    memcpy(dest_buf + 1, src_buf + 1, 10);
    ASSERT_MEM_EQ(dest_buf + 1, src_buf + 1, 10, "Both unaligned memcpy failed");
}

static void test_memcpy_large(void) {
    size_t size = 4096;
    char *src = kmalloc(size);
    char *dest = kmalloc(size);

    if (!src || !dest) {
        kprint("SKIP: test_memcpy_large (OOM)\n");
        if (src) kfree(src, size);
        if (dest) kfree(dest, size);
        return;
    }

    for (size_t i = 0; i < size; i++) src[i] = (char)(i & 0xFF);
    memset(dest, 0, size);

    memcpy(dest, src, size);
    ASSERT_MEM_EQ(dest, src, size, "Large memcpy failed");

    kfree(src, size);
    kfree(dest, size);
}

void run_string_tests(void) {
    kprint("Running String Tests...\n");
    failed_tests = 0;

    test_memcpy_basic();
    test_memcpy_small();
    test_memcpy_unaligned();
    test_memcpy_large();

    if (failed_tests == 0) {
        kprint("String Tests: PASS\n");
    } else {
        kprint("String Tests: FAIL\n");
    }
}
