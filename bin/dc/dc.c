#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../usr.lib/bc/num.h"

#define STACK_SIZE 1024
bc_num *stack[STACK_SIZE];
int sp = 0;

void push(bc_num *v) {
    if (sp < STACK_SIZE) stack[sp++] = v;
    else printf("dc: stack overflow\n");
}

bc_num *pop() {
    if (sp > 0) return stack[--sp];
    printf("dc: stack empty\n");
    return bc_from_long(0);
}

bc_num *peek() {
    if (sp > 0) return stack[sp-1];
    return bc_from_long(0);
}

// Dynamic buffer for input parsing
struct {
    char *data;
    int len;
    int cap;
} input_buf;

void buf_init() {
    input_buf.cap = 1024;
    input_buf.len = 0;
    input_buf.data = malloc(input_buf.cap);
    if (!input_buf.data) {
        perror("dc: malloc");
        exit(1);
    }
}

void buf_append(char c) {
    if (input_buf.len + 1 >= input_buf.cap) {
        input_buf.cap *= 2;
        input_buf.data = realloc(input_buf.data, input_buf.cap);
        if (!input_buf.data) {
            perror("dc: realloc");
            exit(1);
        }
    }
    input_buf.data[input_buf.len++] = c;
}

void buf_reset() {
    input_buf.len = 0;
}

// Flush current number in buffer to stack
void flush_num(int *in_num, int *sign, int *dot_seen) {
    if (*in_num) {
        // Null terminate
        if (input_buf.len + 1 > input_buf.cap) {
             buf_append(0);
        } else {
             input_buf.data[input_buf.len] = 0;
        }

        bc_num *n = bc_from_string(input_buf.data);
        if (*sign < 0 && !bc_is_zero(n)) n->sign = -1;
        push(n);

        buf_reset();
        *in_num = 0;
        *sign = 1;
        *dot_seen = 0;
    }
}

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) { perror("dc"); return 1; }
    }

    buf_init();

    int c;
    int in_num = 0;
    int sign = 1;
    int dot_seen = 0;

    while ((c = fgetc(in)) != EOF) {
        if (isspace(c)) {
            flush_num(&in_num, &sign, &dot_seen);
            continue;
        }
        
        // Number chars (digits or dot)
        if (isdigit(c) || c == '.') {
            if (c == '.' && dot_seen) {
                flush_num(&in_num, &sign, &dot_seen);
                // Start new number with this dot
                in_num = 1;
                buf_append(c);
                dot_seen = 1;
                continue;
            }
            if (c == '.') dot_seen = 1;
            in_num = 1;
            buf_append(c);
            continue;
        }
        
        if (c == '#') {
            while ((c = fgetc(in)) != '\n' && c != EOF);
            continue;
        }

        if (c == '_') {
            flush_num(&in_num, &sign, &dot_seen);
            sign = -1;
            in_num = 1;
            continue;
        }

        // Other char => command. Flush number first.
        flush_num(&in_num, &sign, &dot_seen);

        bc_num *a, *b, *res;
        switch (c) {
            case 'p':
                if (sp > 0) {
                    bc_print(peek());
                    printf("\n");
                } else printf("dc: stack empty\n");
                break;
            case 'n':
                if (sp > 0) {
                    bc_print(pop());
                } else printf("dc: stack empty\n");
                break;
            case 'f':
                for (int i = sp - 1; i >= 0; i--) {
                    bc_print(stack[i]);
                    printf("\n");
                }
                break;
            case 'c':
                while (sp > 0) bc_free(pop());
                sp = 0;
                break;
            case 'd': // Duplicate
                if (sp > 0) push(bc_dup(peek()));
                else printf("dc: stack empty\n");
                break;
            case '+':
                b = pop(); a = pop();
                res = bc_add(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case '-':
                b = pop(); a = pop();
                res = bc_sub(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case '*':
                b = pop(); a = pop();
                res = bc_mul(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case '/':
                b = pop(); a = pop();
                res = bc_div(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case '%':
                b = pop(); a = pop();
                res = bc_mod(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case '^':
                b = pop(); a = pop();
                res = bc_pow(a, b);
                push(res);
                bc_free(a); bc_free(b);
                break;
            case 'q':
                if (input_buf.data) free(input_buf.data);
                return 0;
            default:
                break;
        }
    }
    if (input_buf.data) free(input_buf.data);
    return 0;
}
