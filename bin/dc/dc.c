#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include "num.h"

/* Caps: a huge output base OOMs the printer, and a huge power exponent makes
 * a number with billions of digits — both DoS on untrusted input (DC-04). */
#define DC_OBASE_MAX 65535
#define DC_POW_MAX   1000000

/* Accept the digits valid for the given input base, including A..Z for
 * ibase up to 36 (the old code only accepted A..F) (DC-09). */
static int dc_is_num_char(int c, int base) {
    if (c >= '0' && c <= '9') return 1;
    if (base > 10 && c >= 'A' && c <= 'A' + (base - 11)) return 1;
    return 0;
}

/* Upper bound on a dc register-array index: caps a single array at ~1M
 * entries so a hostile index can neither overflow the size arithmetic nor
 * drive an unbounded allocation (DC-01). */
#define DC_ARRAY_MAX (1 << 20)

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
    if (!s->data) { perror("dc: malloc"); exit(1); }   /* DC-08 */
}

void val_free(dc_val_t *v) {
    if (!v) return;
    if (v->type == VAL_NUM) bc_free(v->v.num);
    else free(v->v.str);
    free(v);
}

dc_val_t *val_new_num(bc_num *n) {
    dc_val_t *v = malloc(sizeof(dc_val_t));
    if (!v) { perror("dc: malloc"); exit(1); }          /* DC-08 */
    v->type = VAL_NUM;
    v->v.num = n;
    return v;
}

dc_val_t *val_new_str(const char *s) {
    dc_val_t *v = malloc(sizeof(dc_val_t));
    if (!v) { perror("dc: malloc"); exit(1); }          /* DC-08 */
    v->type = VAL_STR;
    v->v.str = strdup(s);
    if (!v->v.str) { perror("dc: strdup"); exit(1); }
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
        dc_val_t **tmp = realloc(s->data, s->cap * sizeof(dc_val_t*));
        if (!tmp) { perror("realloc"); exit(1); }
        s->data = tmp;
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

/* Cap nested macro execution so a self-referential macro (e.g.
 * `[lFx]sF lFx`) cannot recurse until the C stack overflows (DC-03). */
#define DC_MAX_EXEC_DEPTH 500
static int exec_depth = 0;

void execute_str(const char *s) {
    input_t in = { .p = s, .f = NULL };
    if (exec_depth >= DC_MAX_EXEC_DEPTH) {
        fprintf(stderr, "dc: macro recursion too deep\n");
        return;
    }
    exec_depth++;
    execute(&in);
    exec_depth--;
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
        
        if (dc_is_num_char(c, bc_ibase) || c == '_' || c == '.') {
            char buf[1024]; int i = 0;
            int sign = 1;
            if (c == '_') { sign = -1; c = get_char(in); }
            while (c != EOF && (dc_is_num_char(c, bc_ibase) || c == '.')) {
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
            size_t i = 0, cap = 1024;
            char *s = malloc(cap);          /* was unchecked (DC-06) */
            int depth = 1;
            if (!s) { perror("dc: malloc"); exit(1); }
            while ((c = get_char(in)) != EOF) {
                if (c == '[') depth++;
                else if (c == ']') {
                    if (--depth == 0) break;
                }
                if (i + 1 >= cap) {
                    if (cap > SIZE_MAX / 2) { free(s); fprintf(stderr, "dc: string too long\n"); exit(1); }
                    cap *= 2;
                    char *tmp = realloc(s, cap);
                    if (!tmp) { free(s); perror("dc: realloc"); exit(1); }
                    s = tmp;
                }
                s[i++] = (char)c;
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
                else if (c == '/' || c == '%') {
                    /* Guard divide/modulo by zero at the dc layer (DC-05). */
                    if (bc_is_zero(bv->v.num)) {
                        fprintf(stderr, "dc: divide by zero\n");
                        push(&main_stack, av); push(&main_stack, bv);
                        break;
                    }
                    res = (c == '/') ? bc_div(av->v.num, bv->v.num)
                                     : bc_mod(av->v.num, bv->v.num);
                } else if (c == '^') {
                    /* Reject an enormous exponent so `2 100000000^` can't build
                     * a billion-digit number and OOM/hang (DC-04). */
                    long e = (long)bc_num_to_long(bv->v.num);
                    if (e > DC_POW_MAX || e < -DC_POW_MAX) {
                        fprintf(stderr, "dc: exponent too large\n");
                        push(&main_stack, av); push(&main_stack, bv);
                        break;
                    }
                    res = bc_pow(av->v.num, bv->v.num);
                }
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
                if (v->type == VAL_NUM) {
                    long long ib = bc_num_to_long(v->v.num);
                    if (ib >= 2 && ib <= 36) bc_ibase = (int)ib;
                    else fprintf(stderr, "dc: ibase must be in [2,36]\n");
                }
                val_free(v);
                break;
            }
            case 'o': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) {
                    long long ob = bc_num_to_long(v->v.num);
                    if (ob >= 2 && ob <= DC_OBASE_MAX) bc_obase = (int)ob;
                    else fprintf(stderr, "dc: obase must be in [2,%d]\n", DC_OBASE_MAX);
                }
                val_free(v);
                break;
            }
            case 'k': {
                dc_val_t *v = pop(&main_stack);
                if (v->type == VAL_NUM) {
                    long long sc = bc_num_to_long(v->v.num);
                    if (sc >= 0) bc_scale = (int)sc;
                    else fprintf(stderr, "dc: scale must be >= 0\n");
                }
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
                    long idxl = bc_num_to_long(idxv->v.num);
                    /*
                     * Bound the index and use size_t, overflow-checked
                     * arithmetic, and a checked realloc.  The old code cast to
                     * int (idx+1 overflowed at INT_MAX), multiplied int*size_t
                     * (wraps on 32-bit -> tiny alloc then OOB memset/store),
                     * left realloc unchecked (NULL deref + leak), and let a
                     * moderate index drive an unbounded allocation (DC-01).
                     */
                    if (idxl < 0 || idxl > DC_ARRAY_MAX) {
                        fprintf(stderr, "dc: array index out of bounds\n");
                    } else {
                        size_t idx = (size_t)idxl;
                        if (idx >= (size_t)regs[r].array_len) {
                            size_t old  = (size_t)regs[r].array_len;
                            size_t need = idx + 1;
                            bc_num **na = realloc(regs[r].array,
                                                  need * sizeof(bc_num *));
                            if (!na) {
                                fprintf(stderr, "dc: out of memory\n");
                                val_free(idxv); val_free(valv);
                                break;
                            }
                            memset(na + old, 0, (need - old) * sizeof(bc_num *));
                            regs[r].array = na;
                            regs[r].array_len = (int)need;
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
                if (v->type == VAL_NUM) quit_levels = (int)bc_num_to_long(v->v.num);
                else fprintf(stderr, "dc: Q requires a numeric value\n");
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
                    if (line[0]) {
                        /* Shell-out from dc input is an RCE vector on untrusted
                         * scripts; require explicit opt-in (DC-02). */
                        if (!getenv("DC_ENABLE_SHELL")) {
                            fprintf(stderr, "dc: '!' shell execution is disabled "
                                            "(set DC_ENABLE_SHELL to enable)\n");
                            break;
                        }
                        char *argv[128];
                        int argc = 0;
                        char *p = line;
                        while (*p && argc < 127) {
                            while (isspace((unsigned char)*p)) p++;
                            if (!*p) break;
                            argv[argc++] = p;
                            while (*p && !isspace((unsigned char)*p)) p++;
                            if (*p) {
                                *p = '\0';
                                p++;
                            }
                        }
                        argv[argc] = NULL;
                        if (argc > 0) {
                            pid_t pid = fork();
                            if (pid == -1) {
                                perror("dc: fork");
                            } else if (pid == 0) {
                                execvp(argv[0], argv);
                                perror(argv[0]);
                                _exit(127);
                            } else {
                                int status;
                                waitpid(pid, &status, 0);
                            }
                        }
                    }
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
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { perror(argv[i]); return 1; }
            input_t in = { .p = NULL, .f = f };
            execute(&in);
            fclose(f);
            if (quit_levels > 0) break;
        }
    } else {
        input_t in = { .p = NULL, .f = stdin };
        execute(&in);
    }
    return 0;
}
