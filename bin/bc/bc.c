/*
 * bc - Basic Calculator (AST-based Interpreter)
 *
 * Implements POSIX.1-2024 and GNU extensions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <setjmp.h>
#include "num.h"

void runtime_error(const char *msg);

/*
 * Upper bound on an array index.  The capacity growth below computes
 * (idx + 1) * 2 and repeatedly doubles new_cap as an int; an unbounded
 * attacker-controlled idx overflows that arithmetic to a small or
 * negative capacity, undersizing the allocation while array[idx] is
 * still written — a heap out-of-bounds write.  POSIX guarantees
 * BC_DIM_MAX >= 65535; capping well below INT_MAX keeps the doubling
 * arithmetic from overflowing.
 */
#define BC_DIM_MAX 16777216  /* 2^24 elements */

// Configuration
int opt_s = 0; // POSIX strict mode
int opt_w = 0; // Warn on extensions
int opt_q = 0; // Quiet
int opt_l = 0; // Math library

/* Checked allocation wrappers.  bc has no way to recover from OOM
 * mid-evaluation, so - like libbc's bc_new/bc_expsize - they report and
 * exit rather than return NULL.  Using them also fixes the array-growth
 * realloc sites, which assigned realloc's result straight back to the
 * array pointer: on failure that lost the old block (leak) and left the
 * following memset dereferencing NULL. */
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { perror("bc: malloc"); exit(1); }
    return p;
}
static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) { perror("bc: calloc"); exit(1); }
    return p;
}
static void *xrealloc(void *o, size_t n) {
    void *p = realloc(o, n);
    if (!p) { perror("bc: realloc"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) { perror("bc: strdup"); exit(1); }
    return p;
}

void bc_ext_warn(const char *msg) {
    if (opt_w && !opt_s) {
        fprintf(stderr, "bc: warning (POSIX extension): %s\n", msg);
    } else if (opt_s) {
        fprintf(stderr, "bc: POSIX error: %s\n", msg);
        exit(1);
    }
}

// ---------------------------------------------------------
// Tokens & AST Data Structures
// ---------------------------------------------------------

struct ast_node;
void ast_free(struct ast_node *n);

typedef enum {
    TOK_EOF = 256,
    TOK_NUM,
    TOK_ID,
    TOK_STR,
    TOK_ASSIGN,     // =
    TOK_ADD_ASSIGN, // +=
    TOK_SUB_ASSIGN, // -=
    TOK_MUL_ASSIGN, // *=
    TOK_DIV_ASSIGN, // /=
    TOK_MOD_ASSIGN, // %=
    TOK_POW_ASSIGN, // ^=
    TOK_INC,        // ++
    TOK_DEC,        // --
    TOK_EQ,         // ==
    TOK_NE,         // !=
    TOK_LE,         // <=
    TOK_GE,         // >=
    TOK_AND,        // &&
    TOK_OR,         // ||
    TOK_NOT,        // !
    
    // Keywords
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR,
    TOK_BREAK, TOK_CONTINUE, TOK_RETURN, TOK_HALT, TOK_QUIT,
    TOK_DEFINE, TOK_AUTO, TOK_PRINT, TOK_READ,
    TOK_LENGTH, TOK_SCALE_FUNC, TOK_SQRT
} token_t;

typedef enum {
    AST_NONE = 0,
    AST_NUM,
    AST_VAR,
    AST_ARRAY,
    AST_STR,
    AST_ASSIGN,
    AST_BINOP,
    AST_UNOP,
    AST_PREINC, AST_POSTINC,
    AST_PREDEC, AST_POSTDEC,
    AST_CALL,
    AST_PRINT,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN,
    AST_HALT,
    AST_FUNC,
    AST_BLOCK
} ast_type_t;

typedef struct expr_list {
    struct ast_node *expr;
    struct expr_list *next;
} expr_list_t;

typedef struct ast_node {
    ast_type_t type;
    union {
        // Values
        bc_num *num;
        char *str;
        char *id;
        
        // Arrays (id[idx])
        struct { char *name; struct ast_node *idx; } arr;
        
        // Operations
        struct { int op; struct ast_node *left; struct ast_node *right; } binop;
        struct { int op; struct ast_node *expr; } unop;
        
        // Assignment
        struct { int op; struct ast_node *lval; struct ast_node *rval; } assign;
        
        // Control flow
        struct { struct ast_node *cond; struct ast_node *if_true; struct ast_node *if_false; } if_stmt;
        struct { struct ast_node *cond; struct ast_node *body; } while_stmt;
        struct { struct ast_node *init; struct ast_node *cond; struct ast_node *inc; struct ast_node *body; } for_stmt;
        
        // Block
        struct { struct ast_node **stmts; int count; } block;
        
        // Print
        struct { expr_list_t *args; } print_stmt;
        
        // Function Call
        struct { char *name; expr_list_t *args; } call;
        
        // Function Def
        struct { char *name; expr_list_t *params; expr_list_t *autos; struct ast_node *body; } func;
        
        // Return
        struct { struct ast_node *expr; } ret;
    };
} ast_node_t;

// AST Allocator
ast_node_t *ast_new(ast_type_t type) {
    ast_node_t *n = xcalloc(1, sizeof(ast_node_t));
    n->type = type;
    return n;
}

static void free_expr_list(expr_list_t *p) {
    while (p) {
        expr_list_t *next = p->next;
        ast_free(p->expr);
        free(p);
        p = next;
    }
}

void ast_free(ast_node_t *n) {
    if (!n) return;
    switch (n->type) {
        case AST_NUM: bc_free(n->num); break;
        case AST_VAR: free(n->id); break;
        case AST_STR: free(n->str); break;
        case AST_ARRAY: 
            free(n->arr.name);
            ast_free(n->arr.idx);
            break;
        case AST_BINOP:
            ast_free(n->binop.left);
            ast_free(n->binop.right);
            break;
        case AST_UNOP:
        case AST_PREINC: case AST_POSTINC:
        case AST_PREDEC: case AST_POSTDEC:
            ast_free(n->unop.expr);
            break;
        case AST_ASSIGN:
            ast_free(n->assign.lval);
            ast_free(n->assign.rval);
            break;
        case AST_IF:
            ast_free(n->if_stmt.cond);
            ast_free(n->if_stmt.if_true);
            ast_free(n->if_stmt.if_false);
            break;
        case AST_WHILE:
            ast_free(n->while_stmt.cond);
            ast_free(n->while_stmt.body);
            break;
        case AST_FOR:
            ast_free(n->for_stmt.init);
            ast_free(n->for_stmt.cond);
            ast_free(n->for_stmt.inc);
            ast_free(n->for_stmt.body);
            break;
        case AST_BLOCK:
            for (int i = 0; i < n->block.count; i++) ast_free(n->block.stmts[i]);
            free(n->block.stmts);
            break;
        case AST_RETURN:
            ast_free(n->ret.expr);
            break;
        case AST_PRINT:
            free_expr_list(n->print_stmt.args);
            break;
        case AST_CALL:
            free(n->call.name);
            free_expr_list(n->call.args);
            break;
        case AST_FUNC:
            free(n->func.name);
            free_expr_list(n->func.params);
            free_expr_list(n->func.autos);
            ast_free(n->func.body);
            break;
        default: break;
    }
    free(n);
}

// ---------------------------------------------------------
// Global State & Execution
// ---------------------------------------------------------

bc_num *eval_expr(ast_node_t *n);
void eval_stmt(ast_node_t *n);

typedef struct var_entry {
    char *name;
    bc_num *val;
    bc_num **array;
    int array_len;
    int array_cap;
    struct var_entry *next;
} var_entry_t;

var_entry_t *global_vars = NULL;

var_entry_t *get_global(const char *name) {
    for (var_entry_t *v = global_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    var_entry_t *v = xcalloc(1, sizeof(var_entry_t));
    v->name = xstrdup(name);
    v->val = bc_from_long(0);
    v->next = global_vars;
    global_vars = v;
    return v;
}

typedef struct func_entry {
    char *name;
    ast_node_t *ast; // The AST_FUNC node
    struct func_entry *next;
} func_entry_t;

func_entry_t *functions = NULL;

void register_func(ast_node_t *node) {
    func_entry_t *f = xmalloc(sizeof(func_entry_t));
    f->name = xstrdup(node->func.name);
    f->ast = node;
    f->next = functions;
    functions = f;
}

// ---------------------------------------------------------
// Lexer
// ---------------------------------------------------------

int cur_tok;
char *tok_str = NULL;
bc_num *tok_num = NULL;
int lineno = 1;

/* Recursion guards.  Unbounded function recursion or deeply nested
 * parentheses would otherwise run the C stack off its end and SIGSEGV;
 * these convert that into a clean error.  Reset in bc_reset_after_error
 * (errors longjmp past the normal decrement). */
#define BC_MAX_CALL_DEPTH  1000
#define BC_MAX_PARSE_DEPTH 1000
int call_depth = 0;
int parse_depth = 0;
static FILE *lex_file = NULL;
static const char *lex_string = NULL;

/* Error recovery.  When bc_err_active is set (inside the read/eval
 * loops), an error longjmps back to the loop so the session survives a
 * bad statement instead of exit()ing.  Outside that window (startup,
 * option parsing) errors still terminate. */
jmp_buf bc_err_jmp;
int bc_err_active = 0;

static void bc_fail(void) {
    if (bc_err_active) longjmp(bc_err_jmp, 1);
    exit(1);
}

void lex_error(const char *msg) {
    fprintf(stderr, "bc: syntax error at line %d: %s\n", lineno, msg);
    bc_fail();
}

/* Runtime (evaluation-time) error: undefined function, array used as a
 * scalar, etc.  Reported distinctly from parse errors, and WITHOUT the
 * lexer's line number, which by evaluation time points at wherever the
 * lexer last stopped - not the offending statement. */
void runtime_error(const char *msg) {
    fprintf(stderr, "bc: runtime error: %s\n", msg);
    bc_fail();
}

static void lex_set_file(FILE *fp) {
    lex_file = fp;
    lex_string = NULL;
    lineno = 1;
}

static void lex_set_string(const char *s) {
    lex_file = NULL;
    lex_string = s;
    lineno = 1;
}

/* Set while the string-literal lexer is consuming a "..." body, so the
 * line-continuation handling below leaves "\<newline>" literal inside strings
 * (matching GNU) while still joining continued lines everywhere else. */
static int lex_in_string = 0;

int next_char(void) {
    int c;
    for (;;) {
        if (lex_string) {
            if (*lex_string == '\0') return EOF;
            c = (unsigned char)*lex_string++;
        } else {
            FILE *fp = lex_file ? lex_file : stdin;
            c = fgetc(fp);
        }
        if (c == '\n') lineno++;
        /* A backslash immediately followed by a newline is a line
         * continuation: drop both and keep reading. */
        if (c == '\\' && !lex_in_string) {
            int d;
            if (lex_string) {
                d = *lex_string ? (unsigned char)*lex_string : EOF;
            } else {
                FILE *fp = lex_file ? lex_file : stdin;
                d = fgetc(fp);
            }
            if (d == '\n') {
                if (lex_string) lex_string++;   /* consume the peeked newline */
                lineno++;
                continue;
            }
            if (d != EOF && !lex_string)
                ungetc(d, lex_file ? lex_file : stdin);  /* file: push back peek */
            return '\\';                                  /* string: peek not consumed */
        }
        return c;
    }
}

void unget_char(int c) {
    if (c == '\n') lineno--;
    if (lex_string) {
        if (c != EOF) lex_string--;
        return;
    }
    FILE *fp = lex_file ? lex_file : stdin;
    ungetc(c, fp);
}

/* Growable token buffer: starts in an inline stack array and spills to
 * the heap when a token outgrows it, so numbers, names, and strings are
 * no longer silently truncated at a fixed size (bc is arbitrary
 * precision - a 2000-digit constant must be read whole). */
typedef struct {
    char  *p;
    size_t len, cap;
    char   inln[256];
} lexbuf_t;

static void lb_init(lexbuf_t *b) { b->p = b->inln; b->len = 0; b->cap = sizeof(b->inln); }

static void lb_push(lexbuf_t *b, int c) {
    if (b->len + 1 >= b->cap) {
        size_t nc = b->cap * 2;
        char *np = (b->p == b->inln) ? xmalloc(nc) : xrealloc(b->p, nc);
        if (b->p == b->inln) memcpy(np, b->inln, b->len);
        b->p = np;
        b->cap = nc;
    }
    b->p[b->len++] = (char)c;
}

static char *lb_cstr(lexbuf_t *b) { b->p[b->len] = '\0'; return b->p; }
static void  lb_free(lexbuf_t *b) { if (b->p != b->inln) free(b->p); }

int lex(void) {
    int c;
    while ((c = next_char()) != EOF) {
        if (isspace(c)) {
            if (c == '\n') return cur_tok = '\n';
            continue;
        }

        if (c == '#' || c == '/') {
            if (c == '/') {
                int peek = next_char();
                if (peek != '*') {
                    /* Not a block comment.  Distinguish the /= compound-assign
                     * operator from a bare divide; the dedicated operator
                     * scanner below is unreachable for '/' because this
                     * comment check returns first. */
                    if (peek == '=') return cur_tok = TOK_DIV_ASSIGN;
                    unget_char(peek);
                    return cur_tok = '/';
                }
                // C-style comment /* */
                while ((c = next_char()) != EOF) {
                    if (c == '*') {
                        if ((c = next_char()) == '/') break;
                        unget_char(c);
                    }
                }
                continue;
            } else {
                // # comment (GNU)
                bc_ext_warn("# comments are a GNU extension");
                while ((c = next_char()) != '\n' && c != EOF);
                if (c == EOF) return cur_tok = TOK_EOF;
                return cur_tok = '\n';
            }
        }

        if (isdigit(c) || c == '.' || (c >= 'A' && c <= 'F')) {
            lexbuf_t nb; lb_init(&nb);
            int seen_dot = (c == '.');
            lb_push(&nb, c);

            // POSIX: digits + A-F for high base input
            while ((c = next_char()) != EOF) {
                if (isdigit(c) || (c >= 'A' && c <= 'F')) {
                    lb_push(&nb, c);
                } else if (c == '.') {
                    if (seen_dot) break;
                    seen_dot = 1;
                    lb_push(&nb, c);
                } else {
                    break;
                }
            }
            if (c != EOF) unget_char(c);

            if (tok_num) bc_free(tok_num);
            tok_num = bc_from_string(lb_cstr(&nb), bc_ibase);
            lb_free(&nb);
            return cur_tok = TOK_NUM;
        }

        if (islower(c)) {       /* islower already implies alphabetic */
            lexbuf_t ib; lb_init(&ib);
            lb_push(&ib, c);

            while ((c = next_char()) != EOF) {
                if (isalnum(c) || c == '_') {
                    lb_push(&ib, c);
                } else {
                    break;
                }
            }
            if (c != EOF) unget_char(c);
            char *buf = lb_cstr(&ib);

            if (ib.len > 1) bc_ext_warn("multi-character names are a GNU extension");

            int kw = 0;
            if      (strcmp(buf, "if") == 0)       kw = TOK_IF;
            else if (strcmp(buf, "else") == 0)     kw = TOK_ELSE;
            else if (strcmp(buf, "while") == 0)    kw = TOK_WHILE;
            else if (strcmp(buf, "for") == 0)      kw = TOK_FOR;
            else if (strcmp(buf, "break") == 0)    kw = TOK_BREAK;
            else if (strcmp(buf, "continue") == 0) kw = TOK_CONTINUE;
            else if (strcmp(buf, "return") == 0)   kw = TOK_RETURN;
            else if (strcmp(buf, "halt") == 0)     kw = TOK_HALT;
            /* `quit` terminates when the parser reaches it (see
             * parse_statement), whether or not it would execute - unlike
             * `halt`, which stops only when executed. */
            else if (strcmp(buf, "quit") == 0)     kw = TOK_QUIT;
            else if (strcmp(buf, "define") == 0)   kw = TOK_DEFINE;
            else if (strcmp(buf, "auto") == 0)     kw = TOK_AUTO;
            else if (strcmp(buf, "print") == 0)  { bc_ext_warn("print statement is a GNU extension"); kw = TOK_PRINT; }
            else if (strcmp(buf, "read") == 0)     kw = TOK_READ;
            else if (strcmp(buf, "length") == 0)   kw = TOK_LENGTH;
            else if (strcmp(buf, "scale") == 0)    kw = TOK_SCALE_FUNC;
            else if (strcmp(buf, "sqrt") == 0)     kw = TOK_SQRT;

            if (!kw) {
                if (tok_str) free(tok_str);
                tok_str = xstrdup(buf);
                kw = TOK_ID;
            }
            lb_free(&ib);
            return cur_tok = kw;
        }

        if (c == '"') {
             lexbuf_t sb; lb_init(&sb);
             lex_in_string = 1;
             while ((c = next_char()) != '"' && c != EOF) {
                 lb_push(&sb, c);
             }
             lex_in_string = 0;
             if (tok_str) free(tok_str);
             tok_str = xstrdup(lb_cstr(&sb));
             lb_free(&sb);
             return cur_tok = TOK_STR;
        }

        // Two-char operators
        int next = next_char();
        if (c == '=') {
            if (next == '=') return cur_tok = TOK_EQ;
            unget_char(next);
            return cur_tok = TOK_ASSIGN;
        }
        if (c == '+') {
            if (next == '=') return cur_tok = TOK_ADD_ASSIGN;
            if (next == '+') return cur_tok = TOK_INC;
            unget_char(next);
            return cur_tok = '+';
        }
        if (c == '-') {
            if (next == '=') return cur_tok = TOK_SUB_ASSIGN;
            if (next == '-') return cur_tok = TOK_DEC;
            unget_char(next);
            return cur_tok = '-';
        }
        if (c == '*') {
            if (next == '=') return cur_tok = TOK_MUL_ASSIGN;
            unget_char(next);
            return cur_tok = '*';
        }
        /* '/' (and '/=' and comments) are fully handled by the comment
         * scanner near the top of lex(), so control never reaches here
         * for it - no '/' case is needed in this operator block. */
        if (c == '%') {
            if (next == '=') return cur_tok = TOK_MOD_ASSIGN;
            unget_char(next);
            return cur_tok = '%';
        }
        if (c == '^') {
            if (next == '=') return cur_tok = TOK_POW_ASSIGN;
            unget_char(next);
            return cur_tok = '^';
        }
        if (c == '!') {
            if (next == '=') return cur_tok = TOK_NE;
            unget_char(next);
            return cur_tok = TOK_NOT;
        }
        if (c == '<') {
            if (next == '=') return cur_tok = TOK_LE;
            unget_char(next);
            return cur_tok = '<';
        }
        if (c == '>') {
            if (next == '=') return cur_tok = TOK_GE;
            unget_char(next);
            return cur_tok = '>';
        }
        if (c == '&') {
            if (next == '&') return cur_tok = TOK_AND;
            unget_char(next);
            return cur_tok = '&';
        }
        if (c == '|') {
             if (next == '|') return cur_tok = TOK_OR;
             unget_char(next);
             return cur_tok = '|';
        }
        
        switch (c) {
            case '(': case ')': case '[': case ']': case '{': case '}':
            case ',': case ';':
                unget_char(next);
                return cur_tok = c;
        }
        
        unget_char(next);
        lex_error("invalid character");
    }
    return cur_tok = TOK_EOF;
}

// ---------------------------------------------------------
// Parser
// ---------------------------------------------------------

ast_node_t *parse_expr(void);
ast_node_t *parse_statement(void);
ast_node_t *parse_block(void);

/* Human-readable name for a token, for diagnostics.  Printable single
 * characters render as themselves; named tokens use their spelling. */
static const char *tok_name(int t) {
    static char one[4];
    switch (t) {
        case TOK_EOF:    return "end of input";
        case TOK_NUM:    return "a number";
        case TOK_ID:     return "a name";
        case TOK_STR:    return "a string";
        case '\n':       return "newline";
        case TOK_ASSIGN: return "'='";
        case TOK_EQ:     return "'=='";
        default:
            if (t >= 32 && t < 127) { one[0] = '\''; one[1] = (char)t; one[2] = '\''; one[3] = '\0'; return one; }
            return "token";
    }
}

void match(int tok) {
    if (cur_tok == tok) {
        lex();
    } else {
        fprintf(stderr, "bc: syntax error at line %d: expected %s, got %s\n",
                lineno, tok_name(tok), tok_name(cur_tok));
        bc_fail();          /* recover per BC-02 instead of exit()ing */
    }
}

ast_node_t *parse_primary(void) {
    ast_node_t *n;
    if (cur_tok == TOK_NUM) {
        n = ast_new(AST_NUM);
        n->num = tok_num;
        tok_num = NULL; // hand over ownership
        lex();
        return n;
    }
    if (cur_tok == TOK_STR) {
        n = ast_new(AST_STR);
        n->str = tok_str;
        tok_str = NULL;
        lex();
        return n;
    }
    if (cur_tok == TOK_ID) {
        char *id = xstrdup(tok_str);
        lex();
        if (cur_tok == '[') {
            lex();
            n = ast_new(AST_ARRAY);
            n->arr.name = id;
            /* An empty subscript "name[]" is a whole-array reference, used to
             * pass an array to / declare an array parameter of a function.
             * A NULL idx marks it (the same convention the define-side param
             * parser already uses). */
            n->arr.idx = (cur_tok == ']') ? NULL : parse_expr();
            match(']');
            return n;
        } else if (cur_tok == '(') {
            lex();
            n = ast_new(AST_CALL);
            n->call.name = id;
            n->call.args = NULL;
            expr_list_t **tail = &n->call.args;
            if (cur_tok != ')') {
                while (1) {
                    expr_list_t *item = xcalloc(1, sizeof(expr_list_t));
                    item->expr = parse_expr();
                    *tail = item;
                    tail = &item->next;
                    if (cur_tok == ',') lex();
                    else break;
                }
            }
            match(')');
            return n;
        } else {
            n = ast_new(AST_VAR);
            n->id = id;
            return n;
        }
    }
    if (cur_tok == TOK_LENGTH || cur_tok == TOK_SCALE_FUNC || cur_tok == TOK_SQRT) {
        int func = cur_tok;
        lex();
        if (cur_tok == '(') {
            lex();
            n = ast_new(AST_CALL);
            if (func == TOK_LENGTH) n->call.name = xstrdup("length");
            else if (func == TOK_SCALE_FUNC) n->call.name = xstrdup("scale");
            else n->call.name = xstrdup("sqrt");
            
            expr_list_t *item = xcalloc(1, sizeof(expr_list_t));
            item->expr = parse_expr();
            n->call.args = item;
            match(')');
            return n;
        } else {
            // Treat as variable (only 'scale' is really a variable)
            if (func == TOK_SCALE_FUNC) {
                n = ast_new(AST_VAR);
                n->id = xstrdup("scale");
                return n;
            }
            lex_error("expected '(' after function name");
        }
    }
    if (cur_tok == TOK_READ) {
        /* read() - GNU extension: read one number from stdin at eval
         * time.  Takes no arguments. */
        lex();
        match('(');
        n = ast_new(AST_CALL);
        n->call.name = xstrdup("read");
        n->call.args = NULL;
        match(')');
        return n;
    }
    if (cur_tok == '(') {
        lex();
        n = parse_expr();
        match(')');
        return n;
    }
    lex_error("expected expression primary");
    return NULL;
}

ast_node_t *parse_postfix(void) {
    ast_node_t *n = parse_primary();
    if (cur_tok == TOK_INC) {
        lex();
        ast_node_t *p = ast_new(AST_POSTINC);
        p->unop.expr = n;
        return p;
    }
    if (cur_tok == TOK_DEC) {
        lex();
        ast_node_t *p = ast_new(AST_POSTDEC);
        p->unop.expr = n;
        return p;
    }
    return n;
}

ast_node_t *parse_unary(void) {
    if (cur_tok == '-') {
        lex();
        ast_node_t *n = ast_new(AST_UNOP);
        n->unop.op = '-';
        n->unop.expr = parse_unary();
        return n;
    }
    if (cur_tok == TOK_NOT) {
        lex();
        ast_node_t *n = ast_new(AST_UNOP);
        n->unop.op = '!';
        n->unop.expr = parse_unary();
        return n;
    }
    if (cur_tok == TOK_INC) {
        lex();
        ast_node_t *n = ast_new(AST_PREINC);
        n->unop.expr = parse_unary();
        return n;
    }
    if (cur_tok == TOK_DEC) {
        lex();
        ast_node_t *n = ast_new(AST_PREDEC);
        n->unop.expr = parse_unary();
        return n;
    }
    return parse_postfix();
}

ast_node_t *parse_pow(void) {
    ast_node_t *left = parse_unary();
    if (cur_tok == '^') {
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = '^';
        n->binop.left = left;
        n->binop.right = parse_pow(); // right-associative
        return n;
    }
    return left;
}

ast_node_t *parse_mul(void) {
    ast_node_t *left = parse_pow();
    while (cur_tok == '*' || cur_tok == '/' || cur_tok == '%') {
        int op = cur_tok;
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = op;
        n->binop.left = left;
        n->binop.right = parse_pow();
        left = n;
    }
    return left;
}

ast_node_t *parse_add(void) {
    ast_node_t *left = parse_mul();
    while (cur_tok == '+' || cur_tok == '-') {
        int op = cur_tok;
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = op;
        n->binop.left = left;
        n->binop.right = parse_mul();
        left = n;
    }
    return left;
}

ast_node_t *parse_relational(void) {
    ast_node_t *left = parse_add();
    if (cur_tok == TOK_EQ || cur_tok == TOK_NE || cur_tok == TOK_LE ||
        cur_tok == TOK_GE || cur_tok == '<' || cur_tok == '>') {
        int op = cur_tok;
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = op;
        n->binop.left = left;
        n->binop.right = parse_add();
        return n;
    }
    return left;
}

ast_node_t *parse_logical_and(void) {
    ast_node_t *left = parse_relational();
    while (cur_tok == TOK_AND) {
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = TOK_AND;
        n->binop.left = left;
        n->binop.right = parse_relational();
        left = n;
    }
    return left;
}

ast_node_t *parse_logical_or(void) {
    ast_node_t *left = parse_logical_and();
    while (cur_tok == TOK_OR) {
        lex();
        ast_node_t *n = ast_new(AST_BINOP);
        n->binop.op = TOK_OR;
        n->binop.left = left;
        n->binop.right = parse_logical_and();
        left = n;
    }
    return left;
}

ast_node_t *parse_expr(void) {
    /* Guard against runaway nesting (deeply parenthesized input) driving
     * the recursive-descent parser off the C stack. */
    if (++parse_depth > BC_MAX_PARSE_DEPTH) {
        parse_depth = 0;
        lex_error("expression nested too deeply");
    }
    ast_node_t *left = parse_logical_or();
    ast_node_t *result = left;
    if (cur_tok == TOK_ASSIGN || cur_tok == TOK_ADD_ASSIGN || cur_tok == TOK_SUB_ASSIGN ||
        cur_tok == TOK_MUL_ASSIGN || cur_tok == TOK_DIV_ASSIGN || cur_tok == TOK_MOD_ASSIGN ||
        cur_tok == TOK_POW_ASSIGN) {

        if (left->type != AST_VAR && left->type != AST_ARRAY) {
            lex_error("invalid lvalue in assignment");
        }

        int op = cur_tok;
        lex();
        ast_node_t *n = ast_new(AST_ASSIGN);
        n->assign.op = op;
        n->assign.lval = left;
        n->assign.rval = parse_expr(); // right-associative
        result = n;
    }
    parse_depth--;
    return result;
}


ast_node_t *parse_statement(void) {
    if (cur_tok == '\n' || cur_tok == ';') {
        lex();
        return NULL; // empty stmt
    }
    if (cur_tok == TOK_STR) {
        // POSIX string print
        ast_node_t *n = ast_new(AST_STR);
        n->str = tok_str;
        tok_str = NULL;
        lex();
        return n; // handled in eval as print
    }
    if (cur_tok == TOK_QUIT) {
        /* quit takes effect the moment it is parsed - even inside an
         * unexecuted branch (`if (0) quit` still exits) - which is what
         * distinguishes it from halt.  Statements parsed before it (on
         * earlier lines) have already run, so `1; quit` prints 1. */
        exit(0);
    }
    if (cur_tok == TOK_IF) {
        lex();
        match('(');
        ast_node_t *cond = parse_expr();
        match(')');
        ast_node_t *stmt = parse_statement();
        ast_node_t *else_stmt = NULL;
        if (cur_tok == TOK_ELSE) {
            lex();
            else_stmt = parse_statement();
        }
        ast_node_t *n = ast_new(AST_IF);
        n->if_stmt.cond = cond;
        n->if_stmt.if_true = stmt;
        n->if_stmt.if_false = else_stmt;
        return n;
    }
    if (cur_tok == TOK_WHILE) {
        lex();
        match('(');
        ast_node_t *cond = parse_expr();
        match(')');
        ast_node_t *body = parse_statement();
        ast_node_t *n = ast_new(AST_WHILE);
        n->while_stmt.cond = cond;
        n->while_stmt.body = body;
        return n;
    }
    if (cur_tok == TOK_FOR) {
        lex();
        match('(');
        /* Any of the three clauses may be empty (`for(;;)`); an omitted
         * condition means "always true" (evaluated in AST_FOR). */
        ast_node_t *init = (cur_tok == ';') ? NULL : parse_expr();
        match(';');
        ast_node_t *cond = (cur_tok == ';') ? NULL : parse_expr();
        match(';');
        ast_node_t *inc = (cur_tok == ')') ? NULL : parse_expr();
        match(')');
        ast_node_t *body = parse_statement();
        ast_node_t *n = ast_new(AST_FOR);
        n->for_stmt.init = init;
        n->for_stmt.cond = cond;
        n->for_stmt.inc = inc;
        n->for_stmt.body = body;
        return n;
    }
    if (cur_tok == TOK_BREAK) {
        lex();
        return ast_new(AST_BREAK);
    }
    if (cur_tok == TOK_CONTINUE) {
        lex();
        return ast_new(AST_CONTINUE);
    }
    if (cur_tok == TOK_RETURN) {
        lex();
        ast_node_t *n = ast_new(AST_RETURN);
        if (cur_tok == '(') {
            lex();
            n->ret.expr = parse_expr();
            match(')');
        } else if (cur_tok != '\n' && cur_tok != ';') {
            n->ret.expr = parse_expr();
        } else {
            n->ret.expr = ast_new(AST_NUM);
            n->ret.expr->num = bc_from_long(0);
        }
        return n;
    }
    if (cur_tok == TOK_HALT) {
        lex();
        return ast_new(AST_HALT);
    }
    if (cur_tok == TOK_PRINT) {
        if (opt_s) lex_error("print is a GNU extension (use -s to disable)");
        lex();
        ast_node_t *n = ast_new(AST_PRINT);
        n->print_stmt.args = NULL;
        expr_list_t **tail = &n->print_stmt.args;
        while (1) {
            expr_list_t *item = xcalloc(1, sizeof(expr_list_t));
            if (cur_tok == TOK_STR) {
                ast_node_t *s = ast_new(AST_STR);
                s->str = tok_str;
                tok_str = NULL;
                lex();
                item->expr = s;
            } else {
                item->expr = parse_expr();
            }
            *tail = item;
            tail = &item->next;
            if (cur_tok == ',') lex();
            else break;
        }
        return n;
    }
    if (cur_tok == '{') {
        lex();
        ast_node_t *n = ast_new(AST_BLOCK);
        n->block.stmts = NULL;
        n->block.count = 0;
        int cap = 0;
        while (cur_tok != '}') {
            if (n->block.count >= cap) {
                cap = cap < 8 ? 8 : cap * 2;
                ast_node_t **tmp = realloc(n->block.stmts, cap * sizeof(ast_node_t*));
                if (!tmp) { perror("realloc"); exit(1); }
                n->block.stmts = tmp;
            }
            ast_node_t *stmt = parse_statement();
            if (stmt) n->block.stmts[n->block.count++] = stmt;
        }
        lex(); // }
        return n;
    }
    
    // expression statement
    ast_node_t *n = parse_expr();
    return n;
}

ast_node_t *parse_top_level(void) {
    if (cur_tok == TOK_EOF) return NULL;
    if (cur_tok == '\n' || cur_tok == ';') {
        lex();
        return NULL;
    }
    
    if (cur_tok == TOK_DEFINE) {
        lex();
        if (cur_tok != TOK_ID) lex_error("expected function name");
        char *name = xstrdup(tok_str);
        lex();
        match('(');
        
        expr_list_t *params = NULL;
        expr_list_t **ptail = &params;
        if (cur_tok != ')') {
            while (1) {
                /* A leading '*' marks a call-by-reference array parameter
                 * (GNU extension: "*a[]").  Record it by prefixing the stored
                 * name with '*'; the call binder strips and acts on it. */
                int byref = 0;
                if (cur_tok == '*') { byref = 1; lex(); }
                if (cur_tok != TOK_ID) lex_error("expected param name");
                expr_list_t *item = xcalloc(1, sizeof(expr_list_t));
                ast_node_t *v = ast_new(AST_VAR);
                if (byref) {
                    size_t nm_len = strlen(tok_str) + 2;
                    char *nm = xmalloc(nm_len);
                    snprintf(nm, nm_len, "*%s", tok_str);
                    v->id = nm;
                } else {
                    v->id = xstrdup(tok_str);
                }
                item->expr = v;
                *ptail = item;
                ptail = &item->next;
                lex();

                if (cur_tok == '[') {
                    lex();
                    match(']');
                    /* Whole-array parameter.  id and arr.name alias the same
                     * union offset, so the name pointer is already in place;
                     * do NOT reassign id/arr.name (that would null the name).
                     * Just retag and mark the empty subscript with idx==NULL. */
                    v->type = AST_ARRAY;
                    v->arr.idx = NULL;
                }

                /* '*' is only meaningful on an array parameter (*a[]); on a
                 * scalar it used to be silently accepted and ignored. */
                if (byref && v->type != AST_ARRAY)
                    lex_error("'*' requires an array parameter");

                if (cur_tok == ',') lex();
                else break;
            }
        }
        match(')');
        match('{');

        // optional auto -- may sit on its own line after the brace, so skip
        // the intervening newline(s) before looking for the keyword.
        while (cur_tok == '\n') lex();
        expr_list_t *autos = NULL;
        expr_list_t **atail = &autos;
        if (cur_tok == TOK_AUTO) {
            lex();
            while (1) {
                /* `scale` lexes as a keyword, not an identifier, but is a
                 * legal auto name (a POSIX special register). */
                const char *aname;
                if (cur_tok == TOK_ID) aname = tok_str;
                else if (cur_tok == TOK_SCALE_FUNC) aname = "scale";
                else lex_error("expected auto var name");
                expr_list_t *item = xcalloc(1, sizeof(expr_list_t));
                ast_node_t *v = ast_new(AST_VAR);
                v->id = xstrdup(aname);
                item->expr = v;
                *atail = item;
                atail = &item->next;
                lex();
                
                if (cur_tok == '[') {
                    lex();
                    match(']');
                    /* Whole-array auto; see the param parser note above. */
                    v->type = AST_ARRAY;
                    v->arr.idx = NULL;
                }
                
                if (cur_tok == ',') lex();
                else break;
            }
            if (cur_tok == '\n' || cur_tok == ';') lex();
        }
        
        ast_node_t *n = ast_new(AST_BLOCK);
        n->block.stmts = NULL;
        n->block.count = 0;
        int cap = 0;
        while (cur_tok != '}') {
            if (n->block.count >= cap) {
                cap = cap < 8 ? 8 : cap * 2;
                ast_node_t **tmp = realloc(n->block.stmts, cap * sizeof(ast_node_t*));
                if (!tmp) { perror("realloc"); exit(1); }
                n->block.stmts = tmp;
            }
            ast_node_t *stmt = parse_statement();
            if (stmt) n->block.stmts[n->block.count++] = stmt;
        }
        lex(); // }
        
        ast_node_t *f = ast_new(AST_FUNC);
        f->func.name = name;
        f->func.params = params;
        f->func.autos = autos;
        f->func.body = n;
        
        register_func(f);
        return NULL; // Declaration handled
    }
    
    return parse_statement();
}

// ---------------------------------------------------------
// Evaluator
// ---------------------------------------------------------

// Local variables stack frame
typedef struct local_var {
    char *name;
    bc_num *val;
    bc_num **array;
    int array_len;
    int array_cap;
    struct local_var *next;
} local_var_t;

/* Pass-by-reference array parameters ("*a[]", a GNU extension) are handled by
 * copy-in/copy-out: the callee works on a private copy, and on return the copy
 * is written back over the caller's array.  This is robust against the callee
 * growing (reallocating) its copy, which true pointer aliasing is not. */
typedef struct byref_bind {
    local_var_t *local;       /* callee's working copy of the array */
    char *srcname;            /* array name to write back to in the caller */
    struct byref_bind *next;
} byref_bind_t;

typedef struct frame {
    local_var_t *locals;
    byref_bind_t *byrefs;
    /* ibase/obase/scale named in the auto list are POSIX "special
     * register" autos: their global value is saved on entry and
     * restored on exit, so the function can set them freely as locals.
     * Bit 0=ibase, 1=obase, 2=scale in reg_saved. */
    int reg_saved;
    int save_ibase, save_obase, save_scale;
    struct frame *prev;
} frame_t;

frame_t *call_stack = NULL;

local_var_t *get_local(const char *name) {
    if (!call_stack) return NULL;
    for (local_var_t *l = call_stack->locals; l; l = l->next) {
        if (strcmp(l->name, name) == 0) return l;
    }
    return NULL;
}

/* Restore any ibase/obase/scale registers this frame saved as autos. */
static void frame_restore_regs(frame_t *fr) {
    if (fr->reg_saved & 1) bc_ibase = fr->save_ibase;
    if (fr->reg_saved & 2) bc_obase = fr->save_obase;
    if (fr->reg_saved & 4) bc_scale = fr->save_scale;
}

bc_num *get_var_val(const char *name) {
    if (strcmp(name, "ibase") == 0) return bc_from_long(bc_ibase);
    if (strcmp(name, "obase") == 0) return bc_from_long(bc_obase);
    if (strcmp(name, "scale") == 0) return bc_from_long(bc_scale);
    
    local_var_t *l = get_local(name);
    if (l) {
        if (!l->val) l->val = bc_from_long(0);
        return bc_dup(l->val);
    }
    var_entry_t *g = get_global(name);
    return bc_dup(g->val);
}

bc_num *get_arr_val(const char *name, int idx) {
    if (idx < 0 || idx >= BC_DIM_MAX) lex_error("array index out of bounds");
    local_var_t *l = get_local(name);
    if (l) {
        if (!l->array) {
            l->array_cap = (idx + 1) < 16 ? 16 : (idx + 1) * 2;
            l->array_len = idx + 1;
            l->array = xcalloc(l->array_cap, sizeof(bc_num*));
        }
        if (idx >= l->array_len) {
            if (idx >= l->array_cap) {
                int new_cap = l->array_cap == 0 ? 16 : l->array_cap * 2;
                while (idx >= new_cap) new_cap *= 2;
                l->array = xrealloc(l->array, new_cap * sizeof(bc_num*));
                memset(l->array + l->array_cap, 0, (new_cap - l->array_cap) * sizeof(bc_num*));
                l->array_cap = new_cap;
            }
            l->array_len = idx + 1;
        }
        if (!l->array[idx]) l->array[idx] = bc_from_long(0);
        return bc_dup(l->array[idx]);
    }
    var_entry_t *g = get_global(name);
    if (!g->array) {
        g->array_cap = (idx + 1) < 16 ? 16 : (idx + 1) * 2;
        g->array_len = idx + 1;
        g->array = xcalloc(g->array_cap, sizeof(bc_num*));
    }
    if (idx >= g->array_len) {
        if (idx >= g->array_cap) {
            int new_cap = g->array_cap == 0 ? 16 : g->array_cap * 2;
            while (idx >= new_cap) new_cap *= 2;
            g->array = xrealloc(g->array, new_cap * sizeof(bc_num*));
            memset(g->array + g->array_cap, 0, (new_cap - g->array_cap) * sizeof(bc_num*));
            g->array_cap = new_cap;
        }
        g->array_len = idx + 1;
    }
    if (!g->array[idx]) g->array[idx] = bc_from_long(0);
    return bc_dup(g->array[idx]);
}

/* Locate an array by name in the current (caller) scope so a function call
 * can pass it by value: returns the element vector and its length (the
 * vector may be NULL / length 0 for a never-populated array). */
static bc_num **find_array(const char *name, int *len_out) {
    local_var_t *l = get_local(name);
    if (l) { *len_out = l->array_len; return l->array; }
    var_entry_t *g = get_global(name);
    *len_out = g->array_len;
    return g->array;
}

void set_var_val(const char *name, bc_num *val) {
    if (strcmp(name, "ibase") == 0) {
        bc_ibase = bc_num_to_long(val);
        if (bc_ibase < 2) bc_ibase = 2;
        if (bc_ibase > 36) bc_ibase = 36;
        return;
    }
    if (strcmp(name, "obase") == 0) {
        bc_obase = bc_num_to_long(val);
        if (bc_obase < 2) bc_obase = 2;
        return;
    }
    if (strcmp(name, "scale") == 0) {
        bc_scale = bc_num_to_long(val);
        if (bc_scale < 0) bc_scale = 0;
        return;
    }
    
    bc_num *v = bc_dup(val);
    local_var_t *l = get_local(name);
    if (l) {
        if (l->val) bc_free(l->val);
        l->val = v;
        return;
    }
    var_entry_t *g = get_global(name);
    if (g->val) bc_free(g->val);
    g->val = v;
}

void set_arr_val(const char *name, int idx, bc_num *val) {
    if (idx < 0 || idx >= BC_DIM_MAX) lex_error("array index out of bounds");
    local_var_t *l = get_local(name);
    if (l) {
        if (!l->array) {
            l->array_cap = (idx + 1) < 16 ? 16 : (idx + 1) * 2;
            l->array_len = idx + 1;
            l->array = xcalloc(l->array_cap, sizeof(bc_num*));
        }
        if (idx >= l->array_len) {
            if (idx >= l->array_cap) {
                int new_cap = l->array_cap == 0 ? 16 : l->array_cap * 2;
                while (idx >= new_cap) new_cap *= 2;
                l->array = xrealloc(l->array, new_cap * sizeof(bc_num*));
                memset(l->array + l->array_cap, 0, (new_cap - l->array_cap) * sizeof(bc_num*));
                l->array_cap = new_cap;
            }
            l->array_len = idx + 1;
        }
        if (l->array[idx]) bc_free(l->array[idx]);
        l->array[idx] = bc_dup(val);
        return;
    }
    var_entry_t *g = get_global(name);
    if (!g->array) {
        g->array_cap = (idx + 1) < 16 ? 16 : (idx + 1) * 2;
        g->array_len = idx + 1;
        g->array = xcalloc(g->array_cap, sizeof(bc_num*));
    }
    if (idx >= g->array_len) {
        if (idx >= g->array_cap) {
            int new_cap = g->array_cap == 0 ? 16 : g->array_cap * 2;
            while (idx >= new_cap) new_cap *= 2;
            g->array = xrealloc(g->array, new_cap * sizeof(bc_num*));
            memset(g->array + g->array_cap, 0, (new_cap - g->array_cap) * sizeof(bc_num*));
            g->array_cap = new_cap;
        }
        g->array_len = idx + 1;
    }
    if (g->array[idx]) bc_free(g->array[idx]);
    g->array[idx] = bc_dup(val);
}

// Evaluation State
int is_returning = 0;
bc_num *ret_val = NULL;
int is_breaking = 0;
int is_continuing = 0;

/* Read an lvalue (AST_VAR or AST_ARRAY), evaluating an array subscript
 * EXACTLY ONCE.  The evaluated index is returned via *idx_out (-1 for a
 * scalar) so the matching lval_set reuses it instead of re-evaluating
 * the subscript - re-evaluation would double-apply side effects like
 * a[i++].  Returns a freshly owned value. */
static bc_num *lval_get(ast_node_t *lv, int *idx_out) {
    if (lv->type == AST_VAR) {
        *idx_out = -1;
        return get_var_val(lv->id);
    }
    bc_num *iv = eval_expr(lv->arr.idx);
    int idx = bc_num_to_long(iv);
    bc_free(iv);
    *idx_out = idx;
    return get_arr_val(lv->arr.name, idx);
}

/* Store into an lvalue using the index captured by lval_get.  set_*_val
 * duplicate `v` internally, so the caller keeps ownership of `v`.
 * Handles AST_ARRAY too, which the old inline inc/dec path did not. */
static void lval_set(ast_node_t *lv, int idx, bc_num *v) {
    if (lv->type == AST_VAR) set_var_val(lv->id, v);
    else set_arr_val(lv->arr.name, idx, v);
}

bc_num *eval_expr(ast_node_t *n) {
    if (!n) return bc_from_long(0);
    
    switch (n->type) {
        case AST_NUM:
            return bc_dup(n->num);
            
        case AST_VAR: {
            return get_var_val(n->id);
        }
        case AST_ARRAY: {
            /* A whole-array reference "name[]" (idx==NULL) is only valid as a
             * function argument, handled in AST_CALL; reaching here means it
             * was used where a scalar value is required. */
            if (!n->arr.idx) runtime_error("array used where a number is required");
            bc_num *idx_v = eval_expr(n->arr.idx);
            int idx = bc_num_to_long(idx_v);
            bc_free(idx_v);
            return get_arr_val(n->arr.name, idx);
        }
            
        case AST_BINOP: {
            bc_num *l = eval_expr(n->binop.left);
            bc_num *r = eval_expr(n->binop.right);
            bc_num *res = NULL;
            switch (n->binop.op) {
                case '+': res = bc_add(l, r); break;
                case '-': res = bc_sub(l, r); break;
                case '*': res = bc_mul(l, r); break;
                case '/': res = bc_div(l, r); break;
                case '%': res = bc_mod(l, r); break;
                case '^': res = bc_pow(l, r); break;
                case TOK_EQ: res = bc_from_long(bc_compare(l, r) == 0); break;
                case TOK_NE: res = bc_from_long(bc_compare(l, r) != 0); break;
                case '<':    res = bc_from_long(bc_compare(l, r) < 0); break;
                case '>':    res = bc_from_long(bc_compare(l, r) > 0); break;
                case TOK_LE: res = bc_from_long(bc_compare(l, r) <= 0); break;
                case TOK_GE: res = bc_from_long(bc_compare(l, r) >= 0); break;
                case TOK_AND: res = bc_from_long(!bc_is_zero(l) && !bc_is_zero(r)); break;
                case TOK_OR:  res = bc_from_long(!bc_is_zero(l) || !bc_is_zero(r)); break;
            }
            bc_free(l); bc_free(r);
            return res;
        }
            
        case AST_UNOP: {
            bc_num *v = eval_expr(n->unop.expr);
            if (n->unop.op == '-') {
                v->sign = -v->sign;
                return v;
            }
            if (n->unop.op == '!') {
                int z = bc_is_zero(v);
                bc_free(v);
                return bc_from_long(z);
            }
            return v;
        }
        
        case AST_ASSIGN: {
            ast_node_t *lv = n->assign.lval;
            bc_num *r = eval_expr(n->assign.rval);
            if (n->assign.op == TOK_ASSIGN) {
                /* Plain assign: subscript evaluated once. */
                if (lv->type == AST_ARRAY) {
                    bc_num *iv = eval_expr(lv->arr.idx);
                    int idx = bc_num_to_long(iv);
                    bc_free(iv);
                    set_arr_val(lv->arr.name, idx, r);
                } else {
                    set_var_val(lv->id, r);
                }
                return r;
            }
            /* Compound assign: read the lvalue (index captured once) and
             * store the result back to the SAME element. */
            int idx;
            bc_num *l = lval_get(lv, &idx);
            bc_num *res = NULL;
            switch (n->assign.op) {
                case TOK_ADD_ASSIGN: res = bc_add(l, r); break;
                case TOK_SUB_ASSIGN: res = bc_sub(l, r); break;
                case TOK_MUL_ASSIGN: res = bc_mul(l, r); break;
                case TOK_DIV_ASSIGN: res = bc_div(l, r); break;
                case TOK_MOD_ASSIGN: res = bc_mod(l, r); break;
                case TOK_POW_ASSIGN: res = bc_pow(l, r); break;
            }
            lval_set(lv, idx, res);
            bc_free(l); bc_free(r);
            return res;                     /* transfer ownership; no extra dup */
        }

        case AST_PREINC:
        case AST_POSTINC:
        case AST_PREDEC:
        case AST_POSTDEC: {
            ast_node_t *lv = n->unop.expr;
            int idx;
            bc_num *l = lval_get(lv, &idx);
            bc_num *one = bc_from_long(1);
            bc_num *res = NULL;
            if (n->type == AST_PREINC || n->type == AST_POSTINC) res = bc_add(l, one);
            else res = bc_sub(l, one);
            bc_free(one);
            lval_set(lv, idx, res);         /* stores back for arrays too */
            if (n->type == AST_POSTINC || n->type == AST_POSTDEC) { bc_free(res); return l; }
            else { bc_free(l); return res; }
        }
            
        case AST_CALL: {
            if (strcmp(n->call.name, "length") == 0) {
                 bc_num *arg = eval_expr(n->call.args->expr);
                 /* GNU bc length(x): number of significant decimal digits.
                  * length(0)=1.  For |x|>=1 it is (integer digits + scale);
                  * for a pure fraction (integer part 0) it is just the scale,
                  * counting every fractional digit incl. leading zeros
                  * (length(.000123)=6).  Compute D = total represented digits
                  * from the trimmed base-100 array. */
                 int L = arg->len;
                 while (L > 0 && arg->digits[L - 1] == 0) L--;
                 long len;
                 if (L == 0) {
                     len = 1;                     /* value is zero */
                 } else {
                     int msd = arg->digits[L - 1];
                     int D = (L - 1) * 2 + (msd >= 10 ? 2 : 1);
                     int int_digits = D - arg->scale;
                     len = (int_digits > 0) ? D : arg->scale;
                 }
                 bc_free(arg);
                 return bc_from_long(len);
            }
            if (strcmp(n->call.name, "scale") == 0) {
                 bc_num *arg = eval_expr(n->call.args->expr);
                 bc_num *res = bc_from_long(arg->scale);
                 bc_free(arg);
                 return res;
            }
            if (strcmp(n->call.name, "sqrt") == 0) {
                 bc_num *arg = eval_expr(n->call.args->expr);
                 bc_num *res = bc_sqrt(arg);
                 bc_free(arg);
                 return res;
            }
            if (strcmp(n->call.name, "read") == 0) {
                 /* Read one line from stdin and parse it as a number in
                  * the current ibase.  EOF yields 0. */
                 char *line = NULL;
                 size_t cap = 0;
                 fflush(stdout);
                 ssize_t len = getline(&line, &cap, stdin);
                 if (len < 0) { free(line); return bc_from_long(0); }
                 bc_num *v = bc_from_string(line, bc_ibase);
                 free(line);
                 return v;
            }
            if (opt_l) {
                /* j(n,x) is the only two-argument math-library function. */
                if (strcmp(n->call.name, "j") == 0 &&
                    n->call.args && n->call.args->next) {
                    bc_num *nord = eval_expr(n->call.args->expr);
                    bc_num *arg  = eval_expr(n->call.args->next->expr);
                    bc_num *res  = bc_math_j(nord, arg);
                    bc_free(nord); bc_free(arg);
                    return res;
                }
                /* Single-letter math builtins.  Only evaluate the argument
                 * once we know the name is actually one of s/c/a/l/e -- a
                 * user function may also have a single-letter name (e.g. p),
                 * and its argument might be a whole-array reference that must
                 * not be passed through eval_expr. */
                char m = n->call.name[0];
                if (n->call.name[1] == '\0' &&
                    (m == 's' || m == 'c' || m == 'a' || m == 'l' || m == 'e') &&
                    n->call.args && !n->call.args->next) {
                    bc_num *arg = eval_expr(n->call.args->expr);
                    bc_num *res = NULL;
                    if (m == 's') res = bc_math_s(arg);
                    else if (m == 'c') res = bc_math_c(arg);
                    else if (m == 'a') res = bc_math_a(arg);
                    else if (m == 'l') res = bc_math_l(arg);
                    else if (m == 'e') res = bc_math_e(arg);
                    bc_free(arg);
                    if (res) return res;
                }
            }
            func_entry_t *f = functions;
            while (f) {
                if (strcmp(f->name, n->call.name) == 0) break;
                f = f->next;
            }
            if (!f) runtime_error("undefined function called");
            
            // Bind parameters in the CALLER's scope, before pushing the new
            // frame.  A scalar parameter takes the value of its argument
            // expression; an array parameter (AST_ARRAY, idx==NULL) takes a
            // whole-array argument "name[]" and is passed BY VALUE -- a deep
            // copy -- per POSIX bc.  Build the locals into a temp list now so
            // the array lookups still resolve against the caller's scope.
            local_var_t *bound = NULL;
            byref_bind_t *byrefs = NULL;
            expr_list_t *fp = f->ast->func.params;
            expr_list_t *ap = n->call.args;
            while (fp && ap) {
                local_var_t *l = xcalloc(1, sizeof(local_var_t));
                int param_is_array = (fp->expr->type == AST_ARRAY);
                const char *pname = param_is_array ? fp->expr->arr.name
                                                   : fp->expr->id;
                /* A '*' prefix marks a by-reference array parameter. */
                int byref = (pname[0] == '*');
                if (byref) pname++;
                l->name = xstrdup(pname);
                if (param_is_array) {
                    if (ap->expr->type != AST_ARRAY || ap->expr->arr.idx != NULL)
                        runtime_error("array argument expected for array parameter");
                    int slen = 0;
                    bc_num **src = find_array(ap->expr->arr.name, &slen);
                    if (src && slen > 0) {
                        l->array_cap = slen < 16 ? 16 : slen;
                        l->array_len = slen;
                        l->array = xcalloc(l->array_cap, sizeof(bc_num *));
                        for (int k = 0; k < slen; k++)
                            if (src[k]) l->array[k] = bc_dup(src[k]);
                    }
                    if (byref) {
                        /* Remember to copy the callee's working array back over
                         * the caller's source array when the function returns. */
                        byref_bind_t *br = xcalloc(1, sizeof(byref_bind_t));
                        br->local = l;
                        br->srcname = xstrdup(ap->expr->arr.name);
                        br->next = byrefs;
                        byrefs = br;
                    }
                } else {
                    l->val = eval_expr(ap->expr); // caller scope
                }
                l->next = bound;
                bound = l;
                fp = fp->next;
                ap = ap->next;
            }

            /* POSIX requires the argument count to match the parameter
             * count.  The old code just stopped at the shorter list, so
             * a missing argument left its parameter unbound and the
             * function body silently read the same-named GLOBAL.  Free
             * the partially-built (not-yet-pushed) locals and error. */
            if (fp || ap) {
                while (bound) {
                    local_var_t *nx = bound->next;
                    if (bound->val) bc_free(bound->val);
                    if (bound->array) {
                        for (int i = 0; i < bound->array_len; i++)
                            if (bound->array[i]) bc_free(bound->array[i]);
                        free(bound->array);
                    }
                    free(bound->name);
                    free(bound);
                    bound = nx;
                }
                while (byrefs) {
                    byref_bind_t *bn = byrefs->next;
                    free(byrefs->srcname);
                    free(byrefs);
                    byrefs = bn;
                }
                runtime_error("wrong number of arguments to function");
            }

            // Push Frame and attach the captured parameter locals.
            frame_t *fr = xcalloc(1, sizeof(frame_t));
            fr->prev = call_stack;
            fr->locals = bound;
            fr->byrefs = byrefs;
            call_stack = fr;

            /* Bound recursion: the frame is on the stack (so error
             * recovery unwinds it), then fail if we are too deep. */
            if (++call_depth > BC_MAX_CALL_DEPTH)
                runtime_error("call stack too deep");

            // Bind Autos
            expr_list_t *fa = f->ast->func.autos;
            while (fa) {
                const char *aname = (fa->expr->type == AST_ARRAY)
                                    ? fa->expr->arr.name : fa->expr->id;
                /* ibase/obase/scale: save + reset the real register
                 * instead of shadowing it with a dead local (which the
                 * get/set specials would ignore anyway). */
                if (strcmp(aname, "ibase") == 0) {
                    fr->save_ibase = bc_ibase; fr->reg_saved |= 1;
                    fa = fa->next; continue;
                }
                if (strcmp(aname, "obase") == 0) {
                    fr->save_obase = bc_obase; fr->reg_saved |= 2;
                    fa = fa->next; continue;
                }
                if (strcmp(aname, "scale") == 0) {
                    fr->save_scale = bc_scale; fr->reg_saved |= 4;
                    fa = fa->next; continue;
                }
                local_var_t *l = xcalloc(1, sizeof(local_var_t));
                l->name = xstrdup(aname);
                l->val = bc_from_long(0);
                l->next = fr->locals;
                fr->locals = l;
                fa = fa->next;
            }
            
            // Save current state
            int old_ret = is_returning;
            bc_num *old_rv = ret_val;
            int old_break = is_breaking;
            int old_cont = is_continuing;
            
            is_returning = 0; ret_val = NULL;
            is_breaking = 0; is_continuing = 0;
            
            eval_stmt(f->ast->func.body);
            
            bc_num *rv = ret_val;
            if (!rv) rv = bc_from_long(0);
            
            // Restore state
            is_returning = old_ret;
            ret_val = old_rv;
            is_breaking = old_break;
            is_continuing = old_cont;

            // Copy-out for by-reference array parameters: overwrite the
            // caller's source array (resolved in fr->prev, else global) with
            // the callee's working copy.  Done before the frame is popped so
            // the working arrays are still alive.
            for (byref_bind_t *br = fr->byrefs; br; ) {
                byref_bind_t *brn = br->next;
                bc_num ***arrp; int *lenp; int *capp;
                local_var_t *sl = NULL;
                if (fr->prev)
                    for (sl = fr->prev->locals; sl; sl = sl->next)
                        if (strcmp(sl->name, br->srcname) == 0) break;
                if (sl) { arrp = &sl->array; lenp = &sl->array_len; capp = &sl->array_cap; }
                else { var_entry_t *g = get_global(br->srcname);
                       arrp = &g->array; lenp = &g->array_len; capp = &g->array_cap; }
                if (*arrp) {
                    for (int i = 0; i < *lenp; i++) if ((*arrp)[i]) bc_free((*arrp)[i]);
                    free(*arrp);
                    *arrp = NULL; *lenp = 0; *capp = 0;
                }
                local_var_t *wl = br->local;
                if (wl->array && wl->array_len > 0) {
                    *arrp = xcalloc(wl->array_len, sizeof(bc_num *));
                    for (int i = 0; i < wl->array_len; i++)
                        if (wl->array[i]) (*arrp)[i] = bc_dup(wl->array[i]);
                    *lenp = wl->array_len; *capp = wl->array_len;
                }
                free(br->srcname);
                free(br);
                br = brn;
            }

            // Pop Frame
            local_var_t *l = fr->locals;
            while (l) {
                local_var_t *next = l->next;
                if (l->val) bc_free(l->val);
                if (l->array) {
                    for (int i = 0; i < l->array_len; i++) if (l->array[i]) bc_free(l->array[i]);
                    free(l->array);
                }
                free(l->name);
                free(l);
                l = next;
            }
            frame_restore_regs(fr);
            call_stack = fr->prev;
            free(fr);
            call_depth--;

            return rv;
        }
            
        default: break;
    }
    return bc_from_long(0);
}

void eval_stmt(ast_node_t *n) {
    if (!n) return;
    if (is_returning || is_breaking || is_continuing) return;
    
    switch (n->type) {
        case AST_BLOCK:
            for (int i = 0; i < n->block.count; i++) {
                eval_stmt(n->block.stmts[i]);
                if (is_returning || is_breaking || is_continuing) break;
            }
            break;
            
        case AST_IF: {
            bc_num *cond = eval_expr(n->if_stmt.cond);
            int is_true = !bc_is_zero(cond);
            bc_free(cond);
            if (is_true) {
                eval_stmt(n->if_stmt.if_true);
            } else if (n->if_stmt.if_false) {
                eval_stmt(n->if_stmt.if_false);
            }
            break;
        }
            
        case AST_WHILE:
            while (1) {
                bc_num *cond = eval_expr(n->while_stmt.cond);
                int is_true = !bc_is_zero(cond);
                bc_free(cond);
                if (!is_true) break;
                
                eval_stmt(n->while_stmt.body);
                
                if (is_returning) break;
                if (is_breaking) { is_breaking = 0; break; }
                if (is_continuing) { is_continuing = 0; }
            }
            break;
            
        case AST_FOR:
            if (n->for_stmt.init) { bc_num *v = eval_expr(n->for_stmt.init); bc_free(v); }

            while (1) {
                int is_true = 1;                /* omitted condition => true */
                if (n->for_stmt.cond) {
                    bc_num *cond = eval_expr(n->for_stmt.cond);
                    is_true = !bc_is_zero(cond);
                    bc_free(cond);
                }
                if (!is_true) break;

                eval_stmt(n->for_stmt.body);

                if (is_returning) break;
                if (is_breaking) { is_breaking = 0; break; }
                if (is_continuing) { is_continuing = 0; }

                if (n->for_stmt.inc) { bc_num *v = eval_expr(n->for_stmt.inc); bc_free(v); }
            }
            break;
            
        case AST_BREAK:
            is_breaking = 1;
            break;
            
        case AST_CONTINUE:
            is_continuing = 1;
            break;
            
        case AST_RETURN:
            is_returning = 1;
            ret_val = eval_expr(n->ret.expr);
            break;
            
        case AST_HALT:
            exit(0);
            
        case AST_PRINT: {
            expr_list_t *p = n->print_stmt.args;
            while (p) {
                if (p->expr->type == AST_STR) {
                    /* The print statement interprets C-style escapes in its
                     * string operands (\n \t \\ \q etc.); bare string
                     * statements print literally, so this is only done here. */
                    for (const char *s = p->expr->str; *s; s++) {
                        if (*s != '\\' || s[1] == '\0') { putchar(*s); continue; }
                        switch (*++s) {
                            case 'a': putchar('\a'); break;
                            case 'b': putchar('\b'); break;
                            case 'f': putchar('\f'); break;
                            case 'n': putchar('\n'); break;
                            case 'r': putchar('\r'); break;
                            case 't': putchar('\t'); break;
                            case 'q': putchar('"');  break;
                            case '\\': putchar('\\'); break;
                            default: putchar('\\'); putchar(*s); break;
                        }
                    }
                } else {
                    bc_num *v = eval_expr(p->expr);
                    bc_print_base(v, bc_obase);
                    bc_free(v);
                }
                p = p->next;
            }
            break;
        }
            
        case AST_STR:
            printf("%s", n->str);
            break;
            
        default: {
            bc_num *v = eval_expr(n);
            /* An expression used as a statement auto-prints its value; only a
             * plain assignment is silent.  Pre/post increment and decrement
             * ARE printing expressions in bc (++x echoes the new value, x++
             * the old), so they must not be lumped in with assignment. */
            /* Suppress the auto-print when a math error (e.g. divide by
             * zero) was reported during this statement: libbc already
             * printed the diagnostic and returned a fallback zero that
             * must not masquerade as a result. */
            if (n->type != AST_ASSIGN && !bc_error_flag) {
                bc_print_base(v, bc_obase);
                printf("\n");           /* every auto-printed value ends its line */
            }
            bc_free(v);
            break;
        }
    }
}

/* After an error longjmps out of the middle of an evaluation, the call
 * stack may hold frames whose locals were never freed and the
 * control-flow flags may be set.  Unwind to global scope so the next
 * statement starts clean.  (The abandoned frames' bc_nums are freed
 * here; nothing else in a bc process holds references to them.) */
static void bc_reset_after_error(void) {
    while (call_stack) {
        frame_t *fr = call_stack;
        local_var_t *l = fr->locals;
        while (l) {
            local_var_t *nx = l->next;
            if (l->val) bc_free(l->val);
            if (l->array) {
                for (int i = 0; i < l->array_len; i++)
                    if (l->array[i]) bc_free(l->array[i]);
                free(l->array);
            }
            free(l->name);
            free(l);
            l = nx;
        }
        for (byref_bind_t *br = fr->byrefs; br; ) {
            byref_bind_t *bn = br->next;
            free(br->srcname);
            free(br);
            br = bn;
        }
        frame_restore_regs(fr);
        call_stack = fr->prev;
        free(fr);
    }
    if (ret_val) { bc_free(ret_val); ret_val = NULL; }
    is_returning = is_breaking = is_continuing = 0;
    call_depth = 0;
    parse_depth = 0;
}

/* Execute one top-level statement, then clear any control-flow flag it
 * left set.  A break/continue/return that reaches here was not consumed
 * by an enclosing loop or function - without this reset every
 * subsequent statement is silently skipped (the eval_stmt guard sees
 * the stuck flag), wedging the rest of the script or session. */
void run_toplevel(ast_node_t *n) {
    bc_error_flag = 0;      /* fresh per statement; checked by the auto-print */
    eval_stmt(n);
    if (is_breaking || is_continuing) {
        fprintf(stderr, "bc: '%s' outside of a loop\n",
                is_breaking ? "break" : "continue");
    } else if (is_returning) {
        fprintf(stderr, "bc: 'return' outside of a function\n");
    }
    if (ret_val) { bc_free(ret_val); ret_val = NULL; }
    is_returning = is_breaking = is_continuing = 0;
}

/* Read and execute a whole FILE-based source stream with error
 * recovery: an error longjmps back to the setjmp below, we unwind and
 * resync to the next line, then keep going - so one bad statement no
 * longer aborts the entire file / stdin stream. */
static void run_file_stream(FILE *fp) {
    lex_set_file(fp);
    bc_err_active = 1;
    if (setjmp(bc_err_jmp)) {
        bc_reset_after_error();
        /* Drop the rest of the offending line so the parser resyncs on a
         * statement boundary instead of mid-expression. */
        while (cur_tok != '\n' && cur_tok != TOK_EOF) lex();
    } else {
        lex();                          /* prime the first token */
    }
    while (cur_tok != TOK_EOF) {
        ast_node_t *n = parse_top_level();
        if (n) { run_toplevel(n); ast_free(n); }
    }
    bc_err_active = 0;
}

/* Interactive REPL.  Reads through the persistent FILE-based lexer on
 * stdin (NOT one getline'd string per line), so a statement that spans
 * lines - `define f(x) {` ... `}`, an open brace or paren - keeps
 * lexing across newlines instead of hitting EOF mid-parse.  The "bc> "
 * prompt is printed before each new input line (i.e. when a statement
 * terminator is seen), so it appears before the blocking read.  Errors
 * recover per BC-02. */
static void run_interactive(void) {
    lex_set_file(stdin);
    bc_err_active = 1;
    if (setjmp(bc_err_jmp)) {
        bc_reset_after_error();
        while (cur_tok != '\n' && cur_tok != TOK_EOF) lex();  /* resync */
    } else {
        printf("bc> ");
        fflush(stdout);
        lex();
    }
    while (cur_tok != TOK_EOF) {
        if (cur_tok == '\n') {
            /* Statement terminator: prompt for the next line, then read
             * it (the read blocks, so the prompt must come first). */
            printf("bc> ");
            fflush(stdout);
            lex();
            continue;
        }
        ast_node_t *n = parse_top_level();
        if (n) { run_toplevel(n); ast_free(n); }
    }
    bc_err_active = 0;
}

int main(int argc, char **argv) {
    int c;
    while ((c = getopt(argc, argv, "hswql")) != -1) {
        switch (c) {
            case 's': opt_s = 1; break;
            case 'w': opt_w = 1; break;
            case 'q': opt_q = 1; break;
            case 'l': opt_l = 1; bc_scale = 20; break;
            case 'h':
                printf("usage: bc [-hswql] [file...]\n");
                return 0;
            default:
                return 1;
        }
    }
    
    /*
     * The startup banner is a courtesy for interactive use only.  Print it
     * to stderr (never stdout — it must not pollute piped/redirected output)
     * and only when standard input is a terminal.  -q suppresses it too.
     */
    if (!opt_q && isatty(STDIN_FILENO)) {
        fprintf(stderr, "bc (Substrate AST Interpreter)\n");
    }

    /* POSIX: after all file operands are read, bc reads standard input.
     * (GNU bc likewise drops to an interactive session after running a
     * file.)  So process the files, then fall through to stdin. */
    for (int i = optind; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if (!fp) {
            perror(argv[i]);
            return 1;
        }
        run_file_stream(fp);
        fclose(fp);
    }

    if (isatty(STDIN_FILENO)) {
        run_interactive();
        return 0;
    }

    run_file_stream(stdin);
    return 0;
}
