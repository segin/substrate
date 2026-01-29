#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../bc/libbc/num.h"

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

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) { perror("dc"); return 1; }
    }

    int c;
    long long num = 0;
    int in_num = 0;
    int sign = 1;

    while ((c = fgetc(in)) != EOF) {
        if (isspace(c)) {
            if (in_num) { 
                push(bc_from_long(num * sign)); 
                num = 0; in_num = 0; sign = 1; 
            }
            continue;
        }
        
        if (isdigit(c)) {
            // Simple parsing for prototype (still long long limit on input parse)
            // TODO: Proper bignum parsing
            in_num = 1;
            num = num * 10 + (c - '0');
            continue;
        }
        
        if (c == '#') {
            while ((c = fgetc(in)) != '\n' && c != EOF);
            continue;
        }

        if (c == '_') {
            sign = -1;
            in_num = 1;
            continue;
        }

        if (in_num) { 
            push(bc_from_long(num * sign)); 
            num = 0; in_num = 0; sign = 1; 
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
                return 0;
            default:
                break;
        }
    }
    return 0;
}
