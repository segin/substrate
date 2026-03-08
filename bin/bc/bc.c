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
#include "../../usr.lib/bc/num.h"

// Configuration
int opt_s = 0; // POSIX strict mode
int opt_w = 0; // Warn on extensions
int opt_q = 0; // Quiet
int opt_l = 0; // Math library

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
    TOK_BREAK, TOK_CONTINUE, TOK_RETURN, TOK_HALT,
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
    ast_node_t *n = calloc(1, sizeof(ast_node_t));
    if (!n) { perror("malloc"); exit(1); }
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
    struct var_entry *next;
} var_entry_t;

var_entry_t *global_vars = NULL;

var_entry_t *get_global(const char *name) {
    if (strcmp(name, "scale") == 0) {
        var_entry_t *v = malloc(sizeof(var_entry_t));
        v->name = strdup(name);
        v->val = bc_from_long(bc_scale);
        v->array = NULL;
        return v; // Fake variable representation
        // Actually, we must bind these special vars during evaluation directly.
    }
    for (var_entry_t *v = global_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    var_entry_t *v = calloc(1, sizeof(var_entry_t));
    v->name = strdup(name);
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
    func_entry_t *f = malloc(sizeof(func_entry_t));
    f->name = strdup(node->func.name);
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
static FILE *lex_file = NULL;
static const char *lex_string = NULL;

void lex_error(const char *msg) {
    fprintf(stderr, "bc: syntax error at line %d: %s\n", lineno, msg);
    exit(1);
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

int next_char(void) {
    int c;
    if (lex_string) {
        if (*lex_string == '\0') return EOF;
        c = (unsigned char)*lex_string++;
    } else {
        FILE *fp = lex_file ? lex_file : stdin;
        c = fgetc(fp);
    }
    if (c == '\n') lineno++;
    return c;
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
            char buf[1024];
            int i = 0;
            int seen_dot = (c == '.');
            buf[i++] = c;
            
            // POSIX: digits + A-F for high base input
            while ((c = next_char()) != EOF) {
                if (isdigit(c) || (c >= 'A' && c <= 'F')) {
                    if (i < 1023) buf[i++] = c;
                } else if (c == '.') {
                    if (seen_dot) break;
                    seen_dot = 1;
                    if (i < 1023) buf[i++] = c;
                } else {
                    break;
                }
            }
            if (c != EOF) unget_char(c);
            buf[i] = '\0';
            
            // If it's just 'A'-'F' and ibase is 10, it could be an ID if we allowed uppercase IDs.
            // But POSIX constants are {0..9, A..F}.
            // Note: If ibase is 10, 'A' is 10.
            
            if (tok_num) bc_free(tok_num);
            tok_num = bc_from_string(buf, bc_ibase); 
            return cur_tok = TOK_NUM;
        }

        if (isalpha(c) && islower(c)) {
            char buf[256];
            int i = 0;
            buf[i++] = c;
            
            while ((c = next_char()) != EOF) {
                if (isalnum(c) || c == '_') {
                    if (i < 255) buf[i++] = c;
                } else {
                    break;
                }
            }
            if (c != EOF) unget_char(c);
            buf[i] = '\0';

            if (strlen(buf) > 1) bc_ext_warn("multi-character names are a GNU extension");

            if (strcmp(buf, "if") == 0) return cur_tok = TOK_IF;
            if (strcmp(buf, "else") == 0) return cur_tok = TOK_ELSE;
            if (strcmp(buf, "while") == 0) return cur_tok = TOK_WHILE;
            if (strcmp(buf, "for") == 0) return cur_tok = TOK_FOR;
            if (strcmp(buf, "break") == 0) return cur_tok = TOK_BREAK;
            if (strcmp(buf, "continue") == 0) return cur_tok = TOK_CONTINUE;
            if (strcmp(buf, "return") == 0) return cur_tok = TOK_RETURN;
            if (strcmp(buf, "halt") == 0) return cur_tok = TOK_HALT;
            if (strcmp(buf, "define") == 0) return cur_tok = TOK_DEFINE;
            if (strcmp(buf, "auto") == 0) return cur_tok = TOK_AUTO;
            if (strcmp(buf, "print") == 0) {
                bc_ext_warn("print statement is a GNU extension");
                return cur_tok = TOK_PRINT;
            }
            if (strcmp(buf, "read") == 0) return cur_tok = TOK_READ;
            if (strcmp(buf, "length") == 0) return cur_tok = TOK_LENGTH;
            if (strcmp(buf, "scale") == 0) return cur_tok = TOK_SCALE_FUNC;
            if (strcmp(buf, "sqrt") == 0) return cur_tok = TOK_SQRT;
            
            if (tok_str) free(tok_str);
            tok_str = strdup(buf);
            return cur_tok = TOK_ID;
        }
        
        if (c == '"') {
             char buf[1024];
             int i = 0;
             while ((c = next_char()) != '"' && c != EOF) {
                 if (i < 1023) buf[i++] = c;
             }
             buf[i] = '\0';
             if (tok_str) free(tok_str);
             tok_str = strdup(buf);
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
        if (c == '/') {
            if (next == '=') return cur_tok = TOK_DIV_ASSIGN;
            unget_char(next); // handled as comments earlier
            return cur_tok = '/';
        }
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

void match(int tok) {
    if (cur_tok == tok) {
        lex();
    } else {
        fprintf(stderr, "bc: syntax error at line %d: expected token %d, got %d\n", lineno, tok, cur_tok);
        exit(1);
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
        char *id = strdup(tok_str);
        lex();
        if (cur_tok == '[') {
            lex();
            n = ast_new(AST_ARRAY);
            n->arr.name = id;
            n->arr.idx = parse_expr();
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
                    expr_list_t *item = calloc(1, sizeof(expr_list_t));
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
            if (func == TOK_LENGTH) n->call.name = strdup("length");
            else if (func == TOK_SCALE_FUNC) n->call.name = strdup("scale");
            else n->call.name = strdup("sqrt");
            
            expr_list_t *item = calloc(1, sizeof(expr_list_t));
            item->expr = parse_expr();
            n->call.args = item;
            match(')');
            return n;
        } else {
            // Treat as variable (only 'scale' is really a variable)
            if (func == TOK_SCALE_FUNC) {
                n = ast_new(AST_VAR);
                n->id = strdup("scale");
                return n;
            }
            lex_error("expected '(' after function name");
        }
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
    ast_node_t *left = parse_logical_or();
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
        return n;
    }
    return left;
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
        ast_node_t *init = parse_expr();
        match(';');
        ast_node_t *cond = parse_expr();
        match(';');
        ast_node_t *inc = parse_expr();
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
            expr_list_t *item = calloc(1, sizeof(expr_list_t));
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
                n->block.stmts = realloc(n->block.stmts, cap * sizeof(ast_node_t*));
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
        char *name = strdup(tok_str);
        lex();
        match('(');
        
        expr_list_t *params = NULL;
        expr_list_t **ptail = &params;
        if (cur_tok != ')') {
            while (1) {
                if (cur_tok != TOK_ID) lex_error("expected param name");
                expr_list_t *item = calloc(1, sizeof(expr_list_t));
                ast_node_t *v = ast_new(AST_VAR);
                v->id = strdup(tok_str);
                item->expr = v;
                *ptail = item;
                ptail = &item->next;
                lex();
                
                if (cur_tok == '[') {
                    lex();
                    match(']');
                    // Array ref parameter... ignore marker in AST for now
                    v->type = AST_ARRAY;
                    v->arr.name = v->id; v->id = NULL;
                    v->arr.idx = NULL; // indicates whole array reference
                }
                
                if (cur_tok == ',') lex();
                else break;
            }
        }
        match(')');
        match('{');
        
        // optional auto
        expr_list_t *autos = NULL;
        expr_list_t **atail = &autos;
        if (cur_tok == TOK_AUTO) {
            lex();
            while (1) {
                if (cur_tok != TOK_ID) lex_error("expected auto var name");
                expr_list_t *item = calloc(1, sizeof(expr_list_t));
                ast_node_t *v = ast_new(AST_VAR);
                v->id = strdup(tok_str);
                item->expr = v;
                *atail = item;
                atail = &item->next;
                lex();
                
                if (cur_tok == '[') {
                    lex();
                    match(']');
                    v->type = AST_ARRAY;
                    v->arr.name = v->id; v->id = NULL;
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
                n->block.stmts = realloc(n->block.stmts, cap * sizeof(ast_node_t*));
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
    struct local_var *next;
} local_var_t;

typedef struct frame {
    local_var_t *locals;
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
    if (idx < 0) lex_error("array index out of bounds");
    local_var_t *l = get_local(name);
    if (l) {
        if (!l->array) {
            l->array_len = idx + 1;
            l->array = calloc(l->array_len, sizeof(bc_num*));
        }
        if (idx >= l->array_len) {
            int old_len = l->array_len;
            l->array_len = idx + 1;
            l->array = realloc(l->array, l->array_len * sizeof(bc_num*));
            memset(l->array + old_len, 0, (l->array_len - old_len) * sizeof(bc_num*));
        }
        if (!l->array[idx]) l->array[idx] = bc_from_long(0);
        return bc_dup(l->array[idx]);
    }
    var_entry_t *g = get_global(name);
    if (!g->array) {
        g->array_len = idx + 1;
        g->array = calloc(g->array_len, sizeof(bc_num*));
    }
    if (idx >= g->array_len) {
        int old_len = g->array_len;
        g->array_len = idx + 1;
        g->array = realloc(g->array, g->array_len * sizeof(bc_num*));
        memset(g->array + old_len, 0, (g->array_len - old_len) * sizeof(bc_num*));
    }
    if (!g->array[idx]) g->array[idx] = bc_from_long(0);
    return bc_dup(g->array[idx]);
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
    if (idx < 0) lex_error("array index out of bounds");
    local_var_t *l = get_local(name);
    if (l) {
        if (!l->array) {
            l->array_len = idx + 1;
            l->array = calloc(l->array_len, sizeof(bc_num*));
        }
        if (idx >= l->array_len) {
            int old_len = l->array_len;
            l->array_len = idx + 1;
            l->array = realloc(l->array, l->array_len * sizeof(bc_num*));
            memset(l->array + old_len, 0, (l->array_len - old_len) * sizeof(bc_num*));
        }
        if (l->array[idx]) bc_free(l->array[idx]);
        l->array[idx] = bc_dup(val);
        return;
    }
    var_entry_t *g = get_global(name);
    if (!g->array) {
        g->array_len = idx + 1;
        g->array = calloc(g->array_len, sizeof(bc_num*));
    }
    if (idx >= g->array_len) {
        int old_len = g->array_len;
        g->array_len = idx + 1;
        g->array = realloc(g->array, g->array_len * sizeof(bc_num*));
        memset(g->array + old_len, 0, (g->array_len - old_len) * sizeof(bc_num*));
    }
    if (g->array[idx]) bc_free(g->array[idx]);
    g->array[idx] = bc_dup(val);
}

// Evaluation State
int is_returning = 0;
bc_num *ret_val = NULL;
int is_breaking = 0;
int is_continuing = 0;

bc_num *eval_expr(ast_node_t *n) {
    if (!n) return bc_from_long(0);
    
    switch (n->type) {
        case AST_NUM:
            return bc_dup(n->num);
            
        case AST_VAR: {
            return get_var_val(n->id);
        }
        case AST_ARRAY: {
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
            bc_num *r = eval_expr(n->assign.rval);
            if (n->assign.op == TOK_ASSIGN) {
                if (n->assign.lval->type == AST_VAR) set_var_val(n->assign.lval->id, r);
                else if (n->assign.lval->type == AST_ARRAY) {
                    bc_num *idx_v = eval_expr(n->assign.lval->arr.idx);
                    int idx = bc_num_to_long(idx_v);
                    bc_free(idx_v);
                    set_arr_val(n->assign.lval->arr.name, idx, r);
                }
                return r;
            }
            bc_num *l = eval_expr(n->assign.lval);
            bc_num *res = NULL;
            switch (n->assign.op) {
                case TOK_ADD_ASSIGN: res = bc_add(l, r); break;
                case TOK_SUB_ASSIGN: res = bc_sub(l, r); break;
                case TOK_MUL_ASSIGN: res = bc_mul(l, r); break;
                case TOK_DIV_ASSIGN: res = bc_div(l, r); break;
                case TOK_MOD_ASSIGN: res = bc_mod(l, r); break;
                case TOK_POW_ASSIGN: res = bc_pow(l, r); break;
            }
            if (n->assign.lval->type == AST_VAR) set_var_val(n->assign.lval->id, res);
            else if (n->assign.lval->type == AST_ARRAY) {
                bc_num *idx_v = eval_expr(n->assign.lval->arr.idx);
                int idx = bc_num_to_long(idx_v);
                bc_free(idx_v);
                set_arr_val(n->assign.lval->arr.name, idx, res);
            }
            bc_free(l); bc_free(r);
            return bc_dup(res); 
        }
        
        case AST_PREINC:
        case AST_POSTINC:
        case AST_PREDEC:
        case AST_POSTDEC: {
            bc_num *l = eval_expr(n->unop.expr);
            bc_num *one = bc_from_long(1);
            bc_num *res = NULL;
            if (n->type == AST_PREINC || n->type == AST_POSTINC) res = bc_add(l, one);
            else res = bc_sub(l, one);
            if (n->unop.expr->type == AST_VAR) set_var_val(n->unop.expr->id, bc_dup(res));
            bc_free(one);
            if (n->type == AST_POSTINC || n->type == AST_POSTDEC) { bc_free(res); return l; }
            else { bc_free(l); return res; }
        }
            
        case AST_CALL: {
            if (strcmp(n->call.name, "length") == 0) {
                 bc_num *arg = eval_expr(n->call.args->expr);
                 int len = arg->len * 2;
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
            if (opt_l) {
                char m = n->call.name[0];
                if (n->call.name[1] == '\0') {
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
            if (!f) lex_error("undefined function called");
            
            // Evaluates arguments before pushing frame
            int argc = 0;
            int args_cap = 0;
            bc_num **args = NULL;
            expr_list_t *p = n->call.args;
            while (p) {
                if (argc >= args_cap) {
                    args_cap = args_cap == 0 ? 4 : args_cap * 2;
                    args = realloc(args, args_cap * sizeof(bc_num*));
                }
                args[argc++] = eval_expr(p->expr);
                p = p->next;
            }
            
            // Push Frame
            frame_t *fr = calloc(1, sizeof(frame_t));
            fr->prev = call_stack;
            call_stack = fr;
            
            // Bind Params
            expr_list_t *fp = f->ast->func.params;
            for (int i = 0; i < argc && fp; i++) {
                local_var_t *l = calloc(1, sizeof(local_var_t));
                l->name = strdup(fp->expr->id);
                l->val = args[i]; // ownership transfer
                l->next = fr->locals;
                fr->locals = l;
                fp = fp->next;
            }
            // Free remaining args if any (unused)
            for (int i = (f->ast->func.params ? argc : 0); i < argc; i++) bc_free(args[i]);
            free(args);
            
            // Bind Autos
            expr_list_t *fa = f->ast->func.autos;
            while (fa) {
                local_var_t *l = calloc(1, sizeof(local_var_t));
                l->name = strdup(fa->expr->id);
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
            call_stack = fr->prev;
            free(fr);
            
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
            { bc_num *init = eval_expr(n->for_stmt.init); bc_free(init); }
            
            while (1) {
                bc_num *cond = eval_expr(n->for_stmt.cond);
                int is_true = !bc_is_zero(cond);
                bc_free(cond);
                if (!is_true) break;
                
                eval_stmt(n->for_stmt.body);
                
                if (is_returning) break;
                if (is_breaking) { is_breaking = 0; break; }
                if (is_continuing) { is_continuing = 0; }
                
                { bc_num *inc = eval_expr(n->for_stmt.inc); bc_free(inc); }
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
                    printf("%s", p->expr->str);
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
            if (n->type != AST_ASSIGN && n->type != AST_PREINC && n->type != AST_POSTINC &&
                n->type != AST_PREDEC && n->type != AST_POSTDEC) {
                bc_print_base(v, bc_obase);
                if (bc_obase <= 16) printf("\n");
            }
            bc_free(v);
            break;
        }
    }
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
    
    if (!opt_q) {
        printf("bc (Substrate AST Interpreter)\n");
    }

    if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                perror(argv[i]);
                return 1;
            }
            lex_set_file(fp);
            lex();
            while (cur_tok != TOK_EOF) {
                ast_node_t *n = parse_top_level();
                if (n) {
                    eval_stmt(n);
                    ast_free(n);
                }
            }
            fclose(fp);
        }
        return 0;
    }

    if (isatty(STDIN_FILENO)) {
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;

        while (1) {
            printf("bc> ");
            fflush(stdout);
            len = getline(&line, &cap, stdin);
            if (len < 0) break;
            lex_set_string(line);
            lex();
            while (cur_tok != TOK_EOF) {
                ast_node_t *n = parse_top_level();
                if (n) {
                    eval_stmt(n);
                    ast_free(n);
                }
            }
        }
        free(line);
        return 0;
    }

    lex_set_file(stdin);
    lex();
    while (cur_tok != TOK_EOF) {
        ast_node_t *n = parse_top_level();
        if (n) {
            eval_stmt(n);
            ast_free(n);
        }
    }
    
    return 0;
}
