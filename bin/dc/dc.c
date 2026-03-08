#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "../../usr.lib/bc/num.h"

typedef enum { VAL_NUM, VAL_STR } val_type_t;

typedef struct {
    val_type_t type;
    union {
        bc_num *num;
        char *str;
    } v;
} dc_val_t;

typedef struct dc_stack {
    dc_val_t **data;
    int sp;
    int cap;
} dc_stack_t;

typedef struct {
    dc_stack_t stack;
    bc_num **array;
    int array_len;
} dc_reg_t;

dc_stack_t main_stack;
dc_reg_t regs[256];
int quit_levels = 0;

void stack_init(dc_stack_t *s) {
    s->cap = 64;
    s->sp = 0;
    s->data = malloc(s->cap * sizeof(dc_val_t*));
}

void val_free(dc_val_t *v) {
    if (!v) return;
    if (v->type == VAL_NUM) bc_free(v->v.num);
    else free(v->v.str);
    free(v);
}

dc_val_t *val_new_num(bc_num *n) {
    dc_val_t *v = malloc(sizeof(dc_val_t));
    v->type = VAL_NUM;
    v->v.num = n;
    return v;
}

dc_val_t *val_new_str(const char *s) {
    dc_val_t *v = malloc(sizeof(dc_val_t));
    v->type = VAL_STR;
    v->v.str = strdup(s);
    return v;
}

dc_val_t *val_dup(dc_val_t *v) {
    if (!v) return NULL;
    if (v->type == VAL_NUM) return val_new_num(bc_dup(v->v.num));
    return val_new_str(v->v.str);
}

void push(dc_stack_t *s, dc_val_t *v) {
    if (s->sp >= s->cap) {
        s->cap *= 2;
        s->data = realloc(s->data, s->cap * sizeof(dc_val_t*));
    }
    s->data[s->sp++] = v;
}

dc_val_t *pop(dc_stack_t *s) {
    if (s->sp <= 0) {
        fprintf(stderr, "dc: stack empty\n");
        return val_new_num(bc_from_long(0));
    }
    return s->data[--s->sp];
}

dc_val_t *peek(dc_stack_t *s) {
    if (s->sp <= 0) {
        fprintf(stderr, "dc: stack empty\n");
        return val_new_num(bc_from_long(0));
    }
    return s->data[s->sp - 1];
}

typedef struct {
    const char *p;
    FILE *f;
} input_t;

int get_char(input_t *in) {
    if (in->p) {
        if (*in->p) return (unsigned char)*in->p++;
        return EOF;
    }
    return fgetc(in->f);
}

void unget_char(input_t *in, int c) {
    if (in->p) {
        if (c != EOF) in->p--;
        return;
    }
    ungetc(c, in->f);
}

void execute(input_t *in);

void execute_str(const char *s) {
    input_t in = { .p = s, .f = NULL };
    execute(&in);
    if (quit_levels > 0) quit_levels--;
}

void do_cond(input_t *in, int op, int negate) {
    int next = get_char(in);
    int r = next;
    int has_equal = 0;
    if (next == '=') {
        has_equal = 1;
        r = get_char(in);
    }
    if (r == EOF) return;

    dc_val_t *bv = pop(&main_stack);
    dc_val_t *av = pop(&main_stack);
    if (av->type != VAL_NUM || bv->type != VAL_NUM) {
        fprintf(stderr, "dc: non-numeric comparison\n");
        push(&main_stack, av); push(&main_stack, bv);
        return;
    }

    int cmp = bc_compare(av->v.num, bv->v.num);
    int ok = 0;
    if (op == '<') {
        if (has_equal) ok = (cmp <= 0);
        else ok = (cmp < 0);
    } else if (op == '>') {
        if (has_equal) ok = (cmp >= 0);
        else ok = (cmp > 0);
    } else if (op == '=') {
        ok = (cmp == 0);
    }

    if (negate) ok = !ok;

    if (ok && regs[r].stack.sp > 0) {
        dc_val_t *v = peek(&regs[r].stack);
        if (v->type == VAL_STR) execute_str(v->v.str);
    }
    val_free(av); val_free(bv);
}

void execute(input_t *in) {
    int c;
    while ((c = get_char(in)) != EOF) {
        if (quit_levels > 0) return;
        if (isspace(c)) continue;
        
        if (isdigit(c) || c == '_' || c == '.' || (bc_ibase > 10 && c >= 'A' && c <= 'F')) {
            char buf[1024]; int i = 0;
            int sign = 1;
            if (c == '_') { sign = -1; c = get_char(in); }
            while (c != EOF && (isdigit(c) || c == '.' || (bc_ibase > 10 && c >= 'A' && c <= 'F'))) {
                if (i < 1023) buf[i++] = c;
                c = get_char(in);
            }
            buf[i] = 0;
            if (c != EOF) unget_char(in, c);
            if (buf[0]) {
                bc_num *n = bc_from_string(buf, bc_ibase);
                if (sign < 0) n->sign = -n->sign;
                push(&main_stack, val_new_num(n));
            }
            continue;
        }

        if (c == '#') {
            while ((c = get_char(in)) != EOF && c != '\n');
            continue;
        }

        if (c == '[') {
            char *s = malloc(1024); int i = 0, cap = 1024;
            int depth = 1;
            while ((c = get_char(in)) != EOF) {
                if (c == '[') depth++;
                else if (c == ']') {
                    if (--depth == 0) break;
                }
                if (i + 1 >= cap) {
                    cap *= 2;
                    s = realloc(s, cap);
                }
                s[i++] = c;
            }
            s[i] = 0;
            push(&main_stack, val_new_str(s));
            free(s);
            continue;
        }

        switch (c) {
            case 'p': {
                dc_val_t *v = peek(&main_stack);
                if (v->type == VAL_NUM) { bc_print_base(v->v.num, bc_obase); printf("\n"); }
                else printf("%s\n", v->v.str);
                break;
            }
            case 'n': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) bc_print_base(v->v.num, bc_obase);
                else printf("%s", v->v.str);
                val_free(v);
                break;
            }
            case 'f':
                for (int i = main_stack.sp - 1; i >= 0; i--) {
                    dc_val_t *v = main_stack.data[i];
                    if (v->type == VAL_NUM) bc_print_base(v->v.num, bc_obase);
                    else printf("%s", v->v.str);
                    printf("\n");
                }
                break;
            case 'c':
                while (main_stack.sp > 0) val_free(pop(&main_stack));
                break;
            case 'd':
                push(&main_stack, val_dup(peek(&main_stack)));
                break;
            case 'r': {
                dc_val_t *a = pop(&main_stack);
                dc_val_t *b = pop(&main_stack);
                push(&main_stack, a);
                push(&main_stack, b);
                break;
            }
            case 'z':
                push(&main_stack, val_new_num(bc_from_long(main_stack.sp)));
                break;
            case '+': case '-': case '*': case '/': case '%': case '^': {
                dc_val_t *bv = pop(&main_stack);
                dc_val_t *av = pop(&main_stack);
                if (av->type != VAL_NUM || bv->type != VAL_NUM) {
                    fprintf(stderr, "dc: non-numeric value\n");
                    push(&main_stack, av); push(&main_stack, bv);
                    break;
                }
                bc_num *res = NULL;
                if (c == '+') res = bc_add(av->v.num, bv->v.num);
                else if (c == '-') res = bc_sub(av->v.num, bv->v.num);
                else if (c == '*') res = bc_mul(av->v.num, bv->v.num);
                else if (c == '/') res = bc_div(av->v.num, bv->v.num);
                else if (c == '%') res = bc_mod(av->v.num, bv->v.num);
                else if (c == '^') res = bc_pow(av->v.num, bv->v.num);
                push(&main_stack, val_new_num(res));
                val_free(av); val_free(bv);
                break;
            }
            case 'v': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) push(&main_stack, val_new_num(bc_sqrt(v->v.num)));
                else fprintf(stderr, "dc: non-numeric value\n");
                val_free(v);
                break;
            }
            case 'i': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) bc_ibase = (int)bc_num_to_long(v->v.num);
                val_free(v);
                break;
            }
            case 'o': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) bc_obase = (int)bc_num_to_long(v->v.num);
                val_free(v);
                break;
            }
            case 'k': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) bc_scale = (int)bc_num_to_long(v->v.num);
                val_free(v);
                break;
            }
            case 'I': push(&main_stack, val_new_num(bc_from_long(bc_ibase))); break;
            case 'O': push(&main_stack, val_new_num(bc_from_long(bc_obase))); break;
            case 'K': push(&main_stack, val_new_num(bc_from_long(bc_scale))); break;
            case '?': {
                char line[1024];
                if (fgets(line, sizeof(line), stdin)) execute_str(line);
                break;
            }
            case 'a': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) {
                    char buf[2] = { (char)bc_num_to_long(v->v.num), 0 };
                    push(&main_stack, val_new_str(buf));
                } else if (v->v.str[0]) {
                    char buf[2] = { v->v.str[0], 0 };
                    push(&main_stack, val_new_str(buf));
                }
                val_free(v);
                break;
            }
            case 'x': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_STR) execute_str(v->v.str);
                else { 
                    // GNU dc executes number by pushing it back? No, it just pops.
                    // But if it's a number, it can't be 'x'd.
                }
                val_free(v);
                break;
            }
            case 's': {
                int r = get_char(in);
                if (r == EOF) break;
                dc_val_t *v = pop(&main_stack);
                if (regs[r].stack.sp > 0) val_free(pop(&regs[r].stack));
                else if (!regs[r].stack.data) stack_init(&regs[r].stack);
                push(&regs[r].stack, v);
                break;
            }
            case 'l': {
                int r = get_char(in);
                if (r == EOF) break;
                if (regs[r].stack.sp > 0) push(&main_stack, val_dup(peek(&regs[r].stack)));
                else push(&main_stack, val_new_num(bc_from_long(0)));
                break;
            }
            case 'S': {
                int r = get_char(in);
                if (r == EOF) break;
                if (!regs[r].stack.data) stack_init(&regs[r].stack);
                push(&regs[r].stack, pop(&main_stack));
                break;
            }
            case 'L': {
                int r = get_char(in);
                if (r == EOF) break;
                if (regs[r].stack.sp > 0) push(&main_stack, pop(&regs[r].stack));
                else push(&main_stack, val_new_num(bc_from_long(0)));
                break;
            }
            case 'Z': {
                dc_val_t *v = pop(&main_stack);
                int l = 0;
                if (v->type == VAL_NUM) l = v->v.num->len; // Rough estimate or full digit count?
                else l = strlen(v->v.str);
                push(&main_stack, val_new_num(bc_from_long(l)));
                val_free(v);
                break;
            }
            case 'X': {
                dc_val_t *v = pop(&main_stack);
                int s = 0;
                if (v->type == VAL_NUM) s = v->v.num->scale;
                push(&main_stack, val_new_num(bc_from_long(s)));
                val_free(v);
                break;
            }
            case ':': {
                int r = get_char(in);
                if (r == EOF) break;
                dc_val_t *idxv = pop(&main_stack);
                dc_val_t *valv = pop(&main_stack);
                if (idxv->type != VAL_NUM || valv->type != VAL_NUM) {
                    fprintf(stderr, "dc: non-numeric array access\n");
                } else {
                    int idx = (int)bc_num_to_long(idxv->v.num);
                    if (idx < 0) fprintf(stderr, "dc: array index out of bounds\n");
                    else {
                        if (!regs[r].array) {
                            regs[r].array_len = idx + 1;
                            regs[r].array = calloc(regs[r].array_len, sizeof(bc_num*));
                        }
                        if (idx >= regs[r].array_len) {
                            int old = regs[r].array_len;
                            regs[r].array_len = idx + 1;
                            regs[r].array = realloc(regs[r].array, regs[r].array_len * sizeof(bc_num*));
                            memset(regs[r].array + old, 0, (regs[r].array_len - old) * sizeof(bc_num*));
                        }
                        if (regs[r].array[idx]) bc_free(regs[r].array[idx]);
                        regs[r].array[idx] = bc_dup(valv->v.num);
                    }
                }
                val_free(idxv); val_free(valv);
                break;
            }
            case ';': {
                int r = get_char(in);
                if (r == EOF) break;
                dc_val_t *idxv = pop(&main_stack);
                if (idxv->type != VAL_NUM) {
                    fprintf(stderr, "dc: non-numeric array access\n");
                } else {
                    int idx = (int)bc_num_to_long(idxv->v.num);
                    if (idx < 0 || !regs[r].array || idx >= regs[r].array_len) {
                        push(&main_stack, val_new_num(bc_from_long(0)));
                    } else {
                        if (regs[r].array[idx]) push(&main_stack, val_new_num(bc_dup(regs[r].array[idx])));
                        else push(&main_stack, val_new_num(bc_from_long(0)));
                    }
                }
                val_free(idxv);
                break;
            }
            case 'q': quit_levels = 2; return;
            case 'Q': {
                dc_val_t *v = pop(&main_stack);
                quit_levels = (int)bc_num_to_long(v->v.num);
                val_free(v);
                return;
            }
            case '!': {
                int next = get_char(in);
                if (next == '<' || next == '>' || next == '=') {
                    do_cond(in, next, 1);
                } else {
                    char line[1024]; int i = 0;
                    while (next != EOF && next != '\n') {
                        if (i < 1023) line[i++] = next;
                        next = get_char(in);
                    }
                    line[i] = 0;
                    if (line[0]) system(line);
                }
                break;
            }
            case '<': case '>': case '=':
                do_cond(in, c, 0);
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    stack_init(&main_stack);
    input_t in = { .p = NULL, .f = stdin };
    if (argc > 1) {
        in.f = fopen(argv[argc-1], "r");
        if (!in.f) { perror("dc"); return 1; }
    }
    execute(&in);
    return 0;
}
