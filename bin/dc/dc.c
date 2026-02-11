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

char *buf = NULL;
int buf_cap = 0;
int buf_len = 0;

void buf_add(int c) {
    if (buf_len + 1 >= buf_cap) {
        buf_cap = buf_cap < 8 ? 8 : buf_cap * 2;
        buf = realloc(buf, buf_cap);
    }
    buf[buf_len++] = c;
    buf[buf_len] = '\0';
}

void buf_reset() {
    buf_len = 0;
    if (buf) buf[0] = '\0';
}

void push_buf(int sign) {
    bc_num *n = bc_from_string(buf);
    if (n->sign != 0) n->sign *= sign;
    push(n);
    buf_reset();
}

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) { perror("dc"); return 1; }
    }

    int c;
    int in_num = 0;
    int sign = 1;

    while ((c = fgetc(in)) != EOF) {
        if (isspace(c)) {
            if (in_num) { 
                push_buf(sign);
                in_num = 0; sign = 1;
            }
            continue;
        }
        
        if (isdigit(c)) {
            in_num = 1;
            buf_add(c);
            continue;
        }
        
        if (c == '#') {
            while ((c = fgetc(in)) != '\n' && c != EOF);
            continue;
        }

        if (c == '_') {
            if (in_num) {
                push_buf(sign);
                sign = 1;
            }
            sign = -1;
            in_num = 1;
            buf_reset();
            continue;
        }

        if (in_num) { 
            push_buf(sign);
            in_num = 0; sign = 1;
        }

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
            case 'd': // Duplicate (deep copy needed for safety?)
                // For now, simple pointer copy is risky if popped and freed
                // TODO: bc_dup
                if (sp > 0) push(bc_from_long(0)); // Placeholder
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
                if (buf) free(buf);
                return 0;
            default:
                break;
        }
    }
    if (buf) free(buf);
    return 0;
}
