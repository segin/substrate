#include "bas.h"

/* Simple tokenizer state */
static const char *cursor;

/* Pre-declarations */
static void parse_expr(void);

static void skip_white() {
    while (*cursor && isspace(*cursor)) cursor++;
}

static int match(const char *kw) {
    int len = strlen(kw);
    if (strncasecmp(cursor, kw, len) == 0 && !isalnum(cursor[len])) {
        cursor += len;
        skip_white();
        return 1;
    }
    return 0;
}

static int consume(char c) {
    if (*cursor == c) {
        cursor++;
        skip_white();
        return 1;
    }
    return 0;
}

static void parse_factor() {
    if (isdigit(*cursor) || *cursor == '.') {
        double v = strtod(cursor, (char**)&cursor);
        space[space_idx].opcode = OP_CONST;
        space[space_idx].arg.f = v;
        space_idx++;
        skip_white();
    } else if (consume('(')) {
        parse_expr();
        if (!consume(')')) {
            /* Error: Missing paren */
        }
    } else {
        /* Variable or Function */
        char name[8];
        int i = 0;
        while (i < 7 && isalnum(*cursor)) name[i++] = *cursor++;
        name[i] = 0;
        /* Not skipping whitespace yet to check for '(' */
        
        /* Check builtins */
        int builtin_id = -1;
        if (strcasecmp(name, "INT") == 0) builtin_id = 0;
        else if (strcasecmp(name, "SQR") == 0) builtin_id = 1;
        else if (strcasecmp(name, "SIN") == 0) builtin_id = 2;
        else if (strcasecmp(name, "COS") == 0) builtin_id = 3;
        else if (strcasecmp(name, "ABS") == 0) builtin_id = 4;
        else if (strcasecmp(name, "RND") == 0) builtin_id = 5;
        else if (strcasecmp(name, "LOG") == 0) builtin_id = 6;
        else if (strcasecmp(name, "EXP") == 0) builtin_id = 7;
        else if (strcasecmp(name, "ATN") == 0) builtin_id = 8;
        
        skip_white();
        
        if (builtin_id >= 0) {
            if (consume('(')) {
                parse_expr();
                if (!consume(')')) { /* Missing ')' */ }
                space[space_idx].opcode = OP_BUILTIN;
                space[space_idx].arg.i = builtin_id;
                space_idx++;
            } else {
                /* Function used without parens? V7 required parens? */
                /* RND sometimes RND without arg. */
                /* For now enforce parens except maybe RND if we want? */
                /* Let's require parens for simplicity match. */
            }
        } else {
            /* Variable */
            /* Truncate to 3 chars for symbols as per old logic or keep full name?
               V7 limited to 1 char + digit? or 2 chars?
               bas.c: strncpy(..., 3) suggests 3 chars?
               Assembly name logic was alpha+digit?
               Let's stick to 3 chars for symtab.
            */
            char sym_name[4];
            strncpy(sym_name, name, 3);
            sym_name[3] = 0;
            
            int sym_idx = add_symbol(sym_name);
            space[space_idx].opcode = OP_VAR_VAL;
            space[space_idx].arg.i = sym_idx;
            space_idx++;
        }
    }
}

static void parse_term() {
    parse_factor();
    while (1) {
        if (consume('*')) {
            parse_factor();
            space[space_idx++].opcode = OP_MUL;
        } else if (consume('/')) {
            parse_factor();
            space[space_idx++].opcode = OP_DIV;
        } else {
            break;
        }
    }
}

static void parse_expr() {
    /* Handle optional unary minus? */
    /* V7 bas e6a handles unary minus. */
    /* Minimal for now. */
    parse_term();
    while (1) {
        if (consume('+')) {
            parse_term();
            space[space_idx++].opcode = OP_ADD;
        } else if (consume('-')) {
            parse_term();
            space[space_idx++].opcode = OP_SUB;
        } else {
            break;
        }
    }
}


int compile_line(int lineno, const char *text, int *out_offset) {
    cursor = text;
    skip_white();
    
    int start = *out_offset;
    
    /* Parse statements */
    while (*cursor) {
        if (match("PRINT")) {
            int suppress_nl = 0;
            /* Parse list... (omitted for brevity, assume previous block logic) */
            /* ACTUALLY, I must replicate logic here if I replace the whole block or be careful. */
            /* I'll use the logic from previous step, assuming it's correct. */
             while (*cursor && *cursor != ':') {
                 if (*cursor == '"') {
                    cursor++;
                    char buf[256];
                    int i = 0;
                    while (*cursor && *cursor != '"') {
                        buf[i++] = *cursor++;
                    }
                    if (*cursor == '"') cursor++;
                    buf[i] = 0;
                    
                    space[*out_offset].opcode = OP_STR;
                    space[*out_offset].arg.s = strdup(buf);
                    (*out_offset)++;
                    skip_white();
                } else if (*cursor == ';' || *cursor == ',') {
                    if (*(cursor+1) == 0 || *(cursor+1) == ':') suppress_nl = 1;
                    cursor++;
                    skip_white();
                    continue;
                } else {
                    parse_expr();
                    space[*out_offset].opcode = OP_PRINT;
                    (*out_offset)++;
                }
            }
            if (!suppress_nl) {
                space[*out_offset].opcode = OP_PRINT_NL;
                (*out_offset)++;
            }
        } else if (match("GOTO")) {
            space[*out_offset].opcode = OP_GOTO;
            int target = atoi(cursor);
            space[*out_offset].arg.target = target;
            while(isdigit(*cursor)) cursor++;
            (*out_offset)++;
        } else if (match("IF")) {
             parse_expr();
             /* Expect THEN or GOTO */
             if (match("THEN") || match("GOTO")) {
                 int target = atoi(cursor);
                 while(isdigit(*cursor)) cursor++;
                 space[*out_offset].opcode = OP_IF;
                 space[*out_offset].arg.target = target;
                 (*out_offset)++;
             }
        } else if (match("LET")) {
             /* LET A = expr */
             /* Parse var name */
             char name[4];
             int i = 0;
             while (i < 3 && isalnum(*cursor)) name[i++] = *cursor++;
             name[i] = 0;
             while (isalnum(*cursor)) cursor++; 
             skip_white();
             
             int sym_idx = add_symbol(name);
             if (consume('=')) {
                 parse_expr();
                 space[*out_offset].opcode = OP_LET;
                 space[*out_offset].arg.i = sym_idx;
                 (*out_offset)++;
             }
        } else if (match("FOR")) {
             /* FOR I = start TO end [STEP s] */
             char name[4];
             int i = 0;
             while (i < 3 && isalnum(*cursor)) name[i++] = *cursor++;
             name[i] = 0;
             while (isalnum(*cursor)) cursor++; 
             skip_white();
             int sym_idx = add_symbol(name);
             
             if (consume('=')) {
                 parse_expr(); /* Init */
                 if (match("TO")) {
                     parse_expr(); /* Limit */
                     
                     if (match("STEP")) {
                         parse_expr(); /* Step */
                     } else {
                         /* Default Step 1 */
                         space[*out_offset].opcode = OP_CONST;
                         space[*out_offset].arg.f = 1.0;
                         (*out_offset)++;
                     }
                     
                     space[*out_offset].opcode = OP_FOR;
                     space[*out_offset].arg.i = sym_idx;
                     (*out_offset)++;
                 }
             }
        } else if (match("NEXT")) {
             /* NEXT I */
             char name[4];
             int i = 0;
             while (i < 3 && isalnum(*cursor)) name[i++] = *cursor++;
             name[i] = 0;
             while (isalnum(*cursor)) cursor++; 
             skip_white();
             int sym_idx = add_symbol(name);
             
             space[*out_offset].opcode = OP_NEXT;
             space[*out_offset].arg.i = sym_idx;
             (*out_offset)++;
             
        } else if (match("GOSUB")) {
             space[*out_offset].opcode = OP_GOSUB;
             int target = atoi(cursor);
             space[*out_offset].arg.target = target;
             while(isdigit(*cursor)) cursor++;
             (*out_offset)++;
             
        } else if (match("RETURN")) {
             space[*out_offset].opcode = OP_RETURN;
             (*out_offset)++;
             
        } else if (isalnum(*cursor)) {
             /* Implicit assignment: A = expr */
             char name[4];
             int i = 0;
             while (i < 3 && isalnum(*cursor)) name[i++] = *cursor++;
             name[i] = 0;
             while (isalnum(*cursor)) cursor++; 
             skip_white();
             
             int sym_idx = add_symbol(name);
             if (consume('=')) {
                 parse_expr();
                 space[*out_offset].opcode = OP_LET;
                 space[*out_offset].arg.i = sym_idx;
                 (*out_offset)++;
             } else {
                 /* Maybe call? Error? */
                 /* Consume remaining */
                 while (*cursor) cursor++;
             }
        } else {
            /* Error */
            while (*cursor) cursor++;
        }
        skip_white();
    }
    
    /* Terminate line code */
    space[*out_offset].opcode = OP_END;
    (*out_offset)++;
    
    return 1;
}

/* Defines for compilation units if separated */
void list_program() {
    for (int i = 0; i < lintab_size; i++) {
        if (lintab[i].text) {
            printf("%d %s\n", lintab[i].lineno, lintab[i].text);
        }
    }
}
