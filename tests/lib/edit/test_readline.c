#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../../lib/edit/el.h"

/* Mock terminal raw/orig to avoid tcgetattr/tcsetattr on pipes */
int terminal_set_raw(EditLine *el) {
    el->term.is_raw = 1;
    return 0;
}

int terminal_set_orig(EditLine *el) {
    el->term.is_raw = 0;
    return 0;
}

int history(History *h, HistEvent *ev, int op, ...) {
    (void)h;
    (void)ev;
    (void)op;
    return -1;
}

/* Include readline.c directly to access static internals */
#include "../../../lib/edit/readline.c"

static EditLine *create_test_el(FILE *fin, FILE *fout) {
    return el_init("test", fin, fout, stderr);
}

static void destroy_test_el(EditLine *el) {
    el_end(el);
}

void test_el_gets_basic(void) {
    int pipe_in[2], pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    FILE *fin  = fdopen(pipe_in[0],  "r");
    FILE *fout = fdopen(pipe_out[1], "w");
    EditLine *el = create_test_el(fin, fout);
    assert(el != NULL);

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

void test_el_gets_backspace(void) {
    int pipe_in[2], pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    FILE *fin  = fdopen(pipe_in[0],  "r");
    FILE *fout = fdopen(pipe_out[1], "w");
    EditLine *el = create_test_el(fin, fout);
    assert(el != NULL);

    /* 'a', 'b', backspace (0x7f), 'c', '\n' */
    const char *input = "ab\x7f" "c\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "ac") == 0);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

void test_el_gets_ctrl_c(void) {
    int pipe_in[2], pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    FILE *fin  = fdopen(pipe_in[0],  "r");
    FILE *fout = fdopen(pipe_out[1], "w");
    EditLine *el = create_test_el(fin, fout);
    assert(el != NULL);

    /* 'a', 'b', Ctrl-C (0x03), 'c', '\n' */
    const char *input = "ab\x03" "c\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "c") == 0);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

void test_el_gets_arrows(void) {
    int pipe_in[2], pipe_out[2];
    assert(pipe(pipe_in) == 0);
    assert(pipe(pipe_out) == 0);

    FILE *fin  = fdopen(pipe_in[0],  "r");
    FILE *fout = fdopen(pipe_out[1], "w");
    EditLine *el = create_test_el(fin, fout);
    assert(el != NULL);

    /* 'a', 'c', Left (ESC [ D), 'b', Right (ESC [ C), 'd', '\n' */
    const char *input = "a\x1b[C\x1b[Dc\x1b[D\x1b[Db\x1b[C\x1b[Cd\n";
    assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));

    int count = 0;
    const char *res = el_gets(el, &count);

    assert(res != NULL);
    assert(strcmp(res, "bcad") == 0);

    close(pipe_in[1]);
    close(pipe_out[0]);
    destroy_test_el(el);
}

int main(void) {
    printf("Running test_readline...\n");
    test_el_gets_basic();
    test_el_gets_backspace();
    test_el_gets_ctrl_c();
    test_el_gets_arrows();
    printf("test_readline passed!\n");
    return 0;
}
