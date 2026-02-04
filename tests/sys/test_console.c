#include <assert.h>
#include <stdio.h>
#include <string.h>

// Mock structures
#define CONSOLE_BUF_SIZE 1024
static char console_in_buf[CONSOLE_BUF_SIZE];
static int console_in_head = 0;
static int console_in_tail = 0;

void mock_console_push_char(char c) {
    int next = (console_in_head + 1) % CONSOLE_BUF_SIZE;
    if (next != console_in_tail) {
        console_in_buf[console_in_head] = c;
        console_in_head = next;
    }
}

char mock_console_getc(void) {
    if (console_in_head == console_in_tail) return 0;
    char c = console_in_buf[console_in_tail];
    console_in_tail = (console_in_tail + 1) % CONSOLE_BUF_SIZE;
    return c;
}

void test_console_ringbuffer() {
    printf("Testing console ring buffer...\n");
    memset(console_in_buf, 0, CONSOLE_BUF_SIZE);
    console_in_head = 0;
    console_in_tail = 0;

    mock_console_push_char('A');
    mock_console_push_char('B');
    assert(mock_console_getc() == 'A');
    assert(mock_console_getc() == 'B');
    assert(mock_console_getc() == 0);

    // Test wrap-around
    console_in_head = CONSOLE_BUF_SIZE - 1;
    console_in_tail = CONSOLE_BUF_SIZE - 1;
    mock_console_push_char('X');
    assert(console_in_head == 0);
    assert(mock_console_getc() == 'X');
    assert(console_in_tail == 0);

    printf("Console ring buffer test passed!\n");
}

int main() {
    test_console_ringbuffer();
    return 0;
}
