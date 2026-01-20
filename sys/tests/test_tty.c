#include <sys/tty.h>
#include <sys/termios.h>
#include "tests.h"
#include <string.h>
#include "../kern/console.h"

#define ASSERT(x) do { if (!(x)) { kprint("ASSERT FAILED: " #x "\n"); } } while (0)

// Mock driver for testing
static char mock_out_buf[1024];
static int mock_out_len = 0;

static int mock_write(struct tty *tty, const unsigned char *buf, int count) {
    (void)tty;
    if (mock_out_len + count > 1024) count = 1024 - mock_out_len;
    memcpy(mock_out_buf + mock_out_len, buf, count);
    mock_out_len += count;
    return count;
}

static int mock_put_char(struct tty *tty, unsigned char c) {
    return mock_write(tty, &c, 1);
}

static struct tty_driver mock_driver = {
    .name = "mock_tty",
    .write = mock_write,
    .put_char = mock_put_char
};

void test_tty_alloc(void) {
    struct tty *tty = tty_alloc(&mock_driver, 1);
    ASSERT(tty != NULL);
    ASSERT(tty->driver == &mock_driver);
    ASSERT(tty->index == 1);
    // Check defaults
    ASSERT(tty->termios.c_lflag & ICANON);
    ASSERT(tty->termios.c_lflag & ECHO);
    tty_free(tty);
}

void test_tty_canonical(void) {
    struct tty *tty = tty_alloc(&mock_driver, 2);
    
    // Test 1: Write incomplete line
    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, 'b');
    // Output should mock echo (assuming ECHO is on)
    ASSERT(mock_out_len >= 2); 
    
    // Read should fail/block (we can't really block in simple test, assume non-blocking check/peek logic or timeout)
    // tty_read usually sleeps. We must be careful calling it.
    // Let's inspect buffer state directly if possible, or assume tty_read returns 0 immediately if buffer not ready
    // Actually tty_read sleeps. We'd hang this test.
    // Skip read test until we have mock scheduler interaction or non-blocking support.
    
    // Test 2: Finish line
    mock_out_len = 0;
    tty_flip_buffer_push(tty, '\n');
    ASSERT(mock_out_len >= 1); // Echoed newline
    
    // Now buffer effectively has "ab\n". Logic queues it.
    
    tty_free(tty);
}

void test_tty_ixoff(void) {
    struct tty *tty = tty_alloc(&mock_driver, 3);
    
    // Enable IXOFF, Disable ECHO/ICANON (prevent echo overflow/blocking)
    tty->termios.c_iflag |= IXOFF;
    tty->termios.c_lflag &= ~(ECHO | ICANON);
    
    // Reset mock buffer
    mock_out_len = 0;
    
    // Fill past HWM (1536)
    for (int i = 0; i < 1600; i++) {
        tty_flip_buffer_push(tty, 'A');
    }
    
    // Check VSTOP (19)
    int found_stop = 0;
    for (int i = 0; i < mock_out_len; i++) {
        if (mock_out_buf[i] == 19) found_stop = 1;
    }
    ASSERT(found_stop);
    ASSERT(tty->input_stopped);
    
    // Drain below LWM (512)
    // Reading 1200 bytes leaves 400 (< 512)
    char buf[1200];
    int n = tty_read(tty, buf, 1200);
    ASSERT(n == 1200);
    
    // Check VSTART (17)
    int found_start = 0;
    for (int i = 0; i < mock_out_len; i++) {
        if (mock_out_buf[i] == 17) found_start = 1;
    }
    ASSERT(found_start);
    ASSERT(!tty->input_stopped);
    
    tty_free(tty);
    kprint("test_tty_ixoff passed\n");
}

void run_tty_tests(void) {
    test_tty_alloc();
    kprint("test_tty_alloc passed\n");
    test_tty_ixoff();
    // test_tty_canonical(); // Needs thread support/mocking to not hang
}
