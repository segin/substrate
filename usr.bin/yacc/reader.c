/*
 * reader.c - Grammar Reader and Lexical Analyzer
 *
 * Implements the lexical analysis and parsing of the Yacc grammar file.
 */

#include "defs.h"

char *line;
int linesize;
bucket *error_symbol;

/* Lexer state */
static char *cptr;
static int peekc = -1;
static char token_buffer[MAXTOKEN];

/* Forward declarations */
static void get_line(void);
static int nextc(void);
static void ungetc_char(int c);
static int keyword(void);
static void skip_comment(void);
static void create_symbol_table(void);
static void copy_action(void);

/* ... */

static void copy_action(void) {
    int c;
    int depth = 1;
    
    fprintf(action_file, "case %d:\n", nrules);
    fprintf(action_file, "{");
    
    while (depth > 0) {
        c = nextc();
        if (c == EOF) {
            fprintf(stderr, "EOF in semantic action\n");
            done(1);
        }
        if (c == '{') depth++;
        if (c == '}') depth--;
        
        if (depth == 0) {
            fprintf(action_file, "}\nbreak;\n"); 
            return;
        }
        
        /* TODO: Translate $$ and $n references */
        
        putc(c, action_file);
    }
}


/* ... */


/* Lexer Implementation */

static int nextc(void) {
    int c;
    if (peekc != -1) {
        c = peekc;
        peekc = -1;
        return c;
    }
    
    if (cptr == NULL || *cptr == '\0') {
        get_line();
        cptr = line;
        if (line[0] == '\0' && feof(input_file))
             return EOF;
        return '\n';
    }
    return *cptr++;
}

static void ungetc_char(int c) {
    peekc = c;
}

static void get_line(void) {
    FILE *f = input_file;
    int c;
    int i;

    if (linesize == 0) {
        linesize = 512;
        line = malloc(linesize);
        if (line == NULL) no_space();
    }

    i = 0;
    lineno++;
    for (;;) {
        c = getc(f);
        if (c == EOF) {
            if (i == 0) {
                line[0] = 0;
                return;
            }
            break;
        }
        if (c == '\n') {
            cptr = NULL; /* Force nextc to return newline then read next line */
            break;
        }
        if (i + 2 >= linesize) {
            linesize *= 2;
            line = realloc(line, linesize);
            if (line == NULL) no_space();
        }
        line[i++] = c;
    }
    line[i] = 0;
    cptr = line;
}

/* Handles comments and whitespace */
static int get_token(void) {
    int c;
    char *bp;

    for (;;) {
        c = nextc();
        if (c == EOF) return 0;
        if (c == '/') {
            int next = nextc();
            if (next == '*') {
                skip_comment();
                continue;
            }
            ungetc_char(next);
        }
        if (!isspace(c)) break;
    }

    if (c == '%') {
        c = nextc();
        if (c == '%') {
            return MARK;  /* %% separator */
        }
        if (c == '{') {
            /* %{ ... %} block - copy to code file */
            /* Skip for now, just consume until %} */
            for (;;) {
                c = nextc();
                if (c == EOF) { fprintf(stderr, "EOF in %%{ block\n"); done(1); }
                if (c == '%') {
                    c = nextc();
                    if (c == '}') break;
                }
            }
            return get_token();  /* Get next token after block */
        }
        /* Keyword after % */
        ungetc_char(c);
        return keyword();
    }
    
    if (isalpha(c) || c == '_' || c == '.' || c == '$') {
        /* Identifier - collect into token_buffer */
        bp = token_buffer;
        do {
            if (bp - token_buffer < MAXTOKEN - 1) *bp++ = c;
            c = nextc();
        } while (isalnum(c) || c == '_' || c == '.' || c == '$');
        ungetc_char(c);
        *bp = '\0';
        return IDENTIFIER;
    }
    
    if (c == '\'') {
        /* Character literal */
        bp = token_buffer;
        c = nextc();
        if (c == '\\') {
            c = nextc();  /* Escape sequence */
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                default: break;
            }
        }
        *bp++ = c;
        *bp = '\0';
        c = nextc();  /* Consume closing quote */
        if (c != '\'') { fprintf(stderr, "Unterminated character literal\n"); }
        return IDENTIFIER;  /* Treat as identifier for symbol lookup */
    }
    
    if (c == '{') return LCURLY;
    if (c == '}') return RCURLY;
    
    return c; /* Single char token like ':' or '|' */
}


static int keyword(void) {
    int c;
    char buf[MAXTOKEN];
    char *bp = buf;
    
    c = nextc();
    while (isalpha(c)) {
        if (bp - buf < MAXTOKEN - 1) *bp++ = c;
        c = nextc();
    }
    ungetc_char(c);
    *bp = '\0';
    
    if (strcmp(buf, "token") == 0) return TERM;
    if (strcmp(buf, "left") == 0) return LEFT;
    if (strcmp(buf, "right") == 0) return RIGHT;
    if (strcmp(buf, "nonassoc") == 0) return NONASSOC;
    if (strcmp(buf, "start") == 0) return START;
    if (strcmp(buf, "type") == 0) return TYPE;
    if (strcmp(buf, "union") == 0) return UNION;
    
    return IDENTIFIER; /* Unknown keyword */
}

static void skip_comment(void) {
    int c;
    int state = 0;
    for (;;) {
        c = nextc();
        if (c == EOF) {
             fprintf(stderr, "Unexpected EOF in comment\n");
             done(1);
        }
        if (state == 0 && c == '*') state = 1;
        else if (state == 1 && c == '/') return;
        else if (state == 1 && c != '*') state = 0;
    }
}


/* Skeletons */
static void parse_declarations(void) {
    int t;
    bucket *bp;

    for (;;) {
        t = get_token();
        if (t == MARK) return; /* %% section separator */
        if (t == 0) return;    /* EOF */
        
        switch (t) {
            case TERM:
            case LEFT:
            case RIGHT:
            case NONASSOC:
                /* Parse token declarations */
                for (;;) {
                    t = get_token();
                    if (t == IDENTIFIER) {
                        bp = lookup(token_buffer);
                        if (bp->class == CLASS_TERM && bp->value != 0) {
                             /* already defined? check for consistency if needed, or redefinition error */
                             /* For now, just re-apply property */
                        }
                        bp->class = CLASS_TERM;
                        if (t == LEFT) bp->assoc = LEFT_ASSOC;
                        else if (t == RIGHT) bp->assoc = RIGHT_ASSOC;
                        else if (t == NONASSOC) bp->assoc = NON_ASSOC;
                        else if (t == TERM) bp->assoc = NO_ASSOC;
                        
                        /* Precedence increases with each declaration line */
                        if (t != TERM) bp->prec = 0; /* TODO: Maintain global precedence counter */
                        
                        /* Optional: Check for explicit token value */
                        /* int validation = get_token(); if (validation == NUMBER) ... */
                        
                    } else if (t == ',') {
                        continue;
                    } else {
                        /* Start of next declaration or type tag */
                        /* If we consumed a token that is NOT an identifier or comma, it is likely the start of the next directive */
                        /* or the %% marker. We must break the inner loop. */
                        /* Since we cannot easily put the token back, we rely on the outer loop re-checking 't' if we handle it correctly. */
                        /* NOTE: The current structure is slightly flawed for lookahead. */
                        /* Fix: Use a 'saved_token' approach or similar if needed. */
                        /* For now, assume we break and the outer loop logic needs to not call get_token() if we already have one. */
                        /* To fix this cleanly: */
                        break; 
                    }
                }
                /* We broke out of the inner loop with 't' holding the next token. */
                /* We need to process this 't' in the outer loop. */
                /* The outer loop does t = get_token() at the top. */
                /* We need to prevent that if we have a valid 't' already. */
                /* Hack for now: This skeleton is imperfect. Real implementation needs 'peek'. */
                /* I will implement 'peek' logic in next step or use unget_token logic if applicable. */
                continue; 
                
            case START:
                t = get_token();
                if (t == IDENTIFIER) {
                     goal_symbol = lookup(token_buffer);
                }
                break;
                
            /* ... other cases ... */
        }
    }
}

static void parse_rules(void) {
    int t;
    /* bucket *lhs = NULL; */
    
    /* Initialize counters */
    nrules = 2; /* Rule 0 is $accept : goal $end, Rule 1 is goal : START_SYM ... but usually Yacc starts with Rule 1 as the first user rule? */
    /* Wait, standard Yacc: Rule 0 is $accept, Rule 1 is first grammar rule OR %start rule */
    /* Let's assume we start filling from index 1. Index 0 is reserved/special? */
    /* Construct rule 0 manually later? */
    nitems = 0;

    t = get_token();
    /* Skip optional %start or other things if handled in declarations, but here we expect rules */
    /* Check for %% if not handled by declarations loop? Declarations loop handles first %%. */
    /* So we are at start of rules. */

    for (;;) {
        if (t == IDENTIFIER) {
            /* Start of a new rule */
            bucket *lhs = lookup(token_buffer);
            if (lhs->class == CLASS_TERM) {
                fprintf(stderr, "LHS cannot be a terminal: %s\n", lhs->name);
                done(1);
            }
            lhs->class = CLASS_NONTERM;
            
            if (goal_symbol == NULL) goal_symbol = lhs;
            
            t = get_token();
            if (t != ':') {
                 fprintf(stderr, "syntax error: expected ':' after %s\n", lhs->name);
                 done(1);
            }
            
            /* Parse RHS alternatives */
            for (;;) {
                if (t == ':') { 
                    /* Initial colonization */ 
                    /* Start a new production */
                    if (nrules >= MAXPROD) no_space();
                    plhs[nrules] = lhs->index;
                    rrhs[nrules] = nitems;
                    rlhs[nrules] = lhs->index; /* Redundant? plhs uses index. rlhs uses index? definitions say short* but array usage implies same */
                    /* Note: defs.h has plhs, rlhs. Usually plhs is sufficient. rlhs might be per-item? */
                    /* Let's stick to: plhs[rule] = lhs_symbol_index. ritem[item_index] = symbol_index. */
                    /* We need to track where a rule starts in ritem. 'rrhs[rule]' can point to start in ritem. */
                } else if (t == '|') {
                     /* End current production, start new one with same LHS */
                     if (nitems >= MAXPROD * 4) no_space();
                     ritem[nitems++] = -nrules; /* End of rule marker? Or 0? Classic yacc uses negated rule number or similar */
                     /* For now, let's use standard yacc convention: */
                     /* ritem contains symbol indices. Terminated by null or negative rule number? */
                     /* mkpar.c usually expects negative rule number at the end of the item list for that rule? */
                     /* Actually, openbsd yacc uses negative rule number in ritem. */
                     ritem[nitems++] = -nrules;
                     nrules++;
                     
                     if (nrules >= MAXPROD) no_space();
                     plhs[nrules] = lhs->index;
                     rrhs[nrules] = nitems;
                } else if (t == ';') {
                     /* End of rule block */
                     if (nitems >= MAXPROD * 4) no_space();
                     ritem[nitems++] = -nrules;
                     nrules++;
                     break;
                } else if (t == IDENTIFIER) {
                    /* Symbol in RHS */
                    bucket *bp = lookup(token_buffer);
                    if (nitems >= MAXPROD * 4) no_space();
                    ritem[nitems++] = bp->index;
                } else if (t == LCURLY) {
                    /* Semantic action */
                    copy_action();
                    /* Action usually creates a dummy non-terminal $$N to hold the action if mid-rule */
                    /* Or effectively attached to text. Classic Yacc treats actions as non-terminals if mid-rule. */
                    /* For end-of-rule actions, it's just code. */
                    /* TODO: Handle mid-rule actions properly. */
                    /* For now, assume actions are handled by copy_action writing to action_file and we rely on output phase to map them. */
                    /* But we need to know WHICH rule has the action. */
                    /* copy_action writes "case N:", where N is the rule number. */
                    /* So we must ensure nrules is synced. */
                } else if (t == MARK || t == 0) {
                     /* unexpected end of rules? */
                     ritem[nitems++] = -nrules;
                     nrules++;
                     break; 
                } else {
                     /* prec? */
                }
                
                t = get_token();
            }
            
            t = get_token(); /* Check next rule or mark */
        } else if (t == MARK || t == 0) {
            break;
        } else {
            fprintf(stderr, "syntax error in rules\n");
            done(1);
        }
    }
}

static void create_symbol_table(void) {
    error_symbol = make_bucket("error");
    error_symbol->class = CLASS_TERM;
}

/* Assign indices and count symbols */
static void pack_symbols(void) {
    bucket *bp;
    int token_value = 257;  /* Token values start at 257 */
    
    ntokens = 0;
    nvars = 0;
    nsyms = 0;
    
    /* First pass: assign indices to terminals */
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM) {
            bp->index = ntokens++;
            if (bp->value == 0) {
                bp->value = token_value++;
            }
        }
    }
    
    /* Second pass: assign indices to non-terminals */
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_NONTERM) {
            bp->index = ntokens + nvars;
            nvars++;
        }
    }
    
    nsyms = ntokens + nvars;
}

void reader(void) {
    create_symbol_table();
    get_line(); /* Prime the pump */
    parse_declarations();
    parse_rules();
    pack_symbols();
    
    /* Debug output */
    fprintf(stderr, "yacc: %d tokens, %d nonterminals, %d rules\n", 
            ntokens, nvars, nrules);
}
