#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../../lib/edit/el.h"

// We'll mock out the terminal set raw and orig functions to avoid dealing with actual tcgetattr/tcsetattr
int terminal_set_raw(EditLine *el) {
    el->term.is_raw = 1;
    return 0;
}

int terminal_set_orig(EditLine *el) {
    el->term.is_raw = 0;
    return 0;
}

// Mocking these to avoid dependency on editline.c
int line_ensure_capacity(EditLine *el, size_t needed) {
    char *new_buf;
    size_t new_cap;

    if (!el) return -1;
    if (needed <= el->line.cap) return 0;

    new_cap = el->line.cap ? el->line.cap : 1024U;
    while (new_cap < needed) {
        new_cap *= 2U;
    }

    new_buf = realloc(el->line.buffer, new_cap);
    if (!new_buf) return -1;

    el->line.buffer = new_buf;
    el->line.cap = new_cap;
    return 0;
}

void el_reset(EditLine *el) {
    if (!el) return;
    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';
}

int history(History *h, HistEvent *ev, int op, ...) {
    (void)h;
    (void)ev;
    (void)op;
    return -1; // Mock history as unavailable
}

// Instead of compiling readline.c, we include it directly so we can test the static functions and el_gets
#include "../../../lib/edit/readline.c"

// Helper function to create an EditLine object for testing
EditLine* create_test_el() {
    EditLine *el = calloc(1, sizeof(EditLine));
    el->line.cap = 1024;
    el->line.buffer = malloc(el->line.cap);
    el->line.buffer[0] = '\0';
    el->line.len = 0;
    el->line.cursor = 0;
    return el;
}

void destroy_test_el(EditLine *el) {
    if (el->fin) fclose(el->fin);
    if (el->fout) fclose(el->fout);
    free(el->line.buffer);
    if (el->render_cache) free(el->render_cache);
    free(el);
}

void test_el_gets_basic() {
    int pipe_in[2];
    int pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    EditLine *el = create_test_el();
    el->fin = fdopen(pipe_in[0], "r");
    el->fout = fdopen(pipe_out[1], "w");

    // Write input into the read end of the pipe
    const char *input = "hello world\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "hello world") == 0);
    assert(count == 11);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

void test_el_gets_backspace() {
    int pipe_in[2];
    int pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    EditLine *el = create_test_el();
    el->fin = fdopen(pipe_in[0], "r");
    el->fout = fdopen(pipe_out[1], "w");

    // 'a', 'b', backspace (0x7f), 'c', '\n'
    const char *input = "ab\x7f""c\n"; // Split string literal to fix hex escape issue
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "ac") == 0);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

void test_el_gets_ctrl_c() {
    int pipe_in[2];
    int pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    EditLine *el = create_test_el();
    el->fin = fdopen(pipe_in[0], "r");
    el->fout = fdopen(pipe_out[1], "w");

    // 'a', 'b', Ctrl-C (0x03), 'c', '\n'
    const char *input = "ab\x03""c\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "c") == 0); // Ctrl-C clears line

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

void test_el_gets_arrows() {
    int pipe_in[2];
    int pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    EditLine *el = create_test_el();
    el->fin = fdopen(pipe_in[0], "r");
    el->fout = fdopen(pipe_out[1], "w");

    // 'a', 'c', Left (ESC [ D), 'b', Right (ESC [ C), 'd', '\n'
    const char *input = "a\x1b[C\x1b[Dc\x1b[D\x1b[Db\x1b[C\x1b[Cd\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "bacd") == 0);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

int main() {
    printf("Running test_readline...\n");
    test_el_gets_basic();
    test_el_gets_backspace();
    test_el_gets_ctrl_c();
    test_el_gets_arrows();
    printf("test_readline passed!\n");
    return 0;
}
