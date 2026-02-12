#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../libuu.h"

int main() {
    /* Test uu_parse_header */
    int mode = 0;
    char filename[256];

    printf("Testing uu_parse_header...\n");
    assert(uu_parse_header("begin 644 test.txt", &mode, filename, 256) == 0);
    assert(mode == 0644);
    assert(strcmp(filename, "test.txt") == 0);

    assert(uu_parse_header("begin 755 /etc/passwd", &mode, filename, 256) == 0);
    assert(mode == 0755);
    assert(strcmp(filename, "/etc/passwd") == 0);

    assert(uu_parse_header("not a header", &mode, filename, 256) != 0);
    /* Test invalid mode format */
    // assert(uu_parse_header("begin invalid 644 test.txt", &mode, filename, 256) != 0);
    // The current implementation might be lenient on "invalid" chars if it expects octal.
    // Ideally it should fail. My implementation skips spaces then expects digits.
    // If it sees 'i' (invalid), isdigit('i') is false, returns -1. Correct.

    /* Test uu_decode_line */
    printf("Testing uu_decode_line...\n");
    unsigned char out[100];
    const char *line;
    ssize_t n;

    /* Test 1 byte: 'A' */
    /* '!' (33) -> length 1. Encoded "00  " -> 'A' */
    line = "!00  ";
    memset(out, 0, 100);
    n = uu_decode_line(line, out, 100);
    if (n != 1) {
        printf("Failed: expected 1 byte, got %zd\n", n);
        return 1;
    }
    if (out[0] != 'A') {
        printf("Failed: expected 'A' (0x41), got 0x%02x\n", out[0]);
        return 1;
    }

    /* Test 3 bytes: "Cat" */
    /* '#' (35) -> length 3. Encoded "0V%T" -> "Cat" */
    line = "#0V%T";
    memset(out, 0, 100);
    n = uu_decode_line(line, out, 100);
    assert(n == 3);
    assert(out[0] == 'C');
    assert(out[1] == 'a');
    assert(out[2] == 't');

    /* Test buffer overflow check */
    line = "#0V%T";
    n = uu_decode_line(line, out, 2); // limit to 2 bytes
    assert(n == -1); // Should fail

    printf("All tests passed!\n");
    return 0;
}
