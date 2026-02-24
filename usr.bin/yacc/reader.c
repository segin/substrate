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

/* ... */

/* Temporary symbol mapping for ritem fixup */
static bucket *temp_sym_map[MAXSYM];
static int temp_sym_count = 0;

/* Deferred rules for mid-rule actions */
static int deferred_rules[MAXPROD];
static int ndeferred = 0;

/* Counter for anonymous non-terminals */
static int gen_sym_count = 0;

static void flush_deferred(void) {
    int i;
    for (i = 0; i < ndeferred; i++) {
        int rule_idx = deferred_rules[i];
        if (nitems >= MAXPROD * 4) no_space();
        rrhs[rule_idx] = nitems;
        ritem[nitems++] = -rule_idx;
    }
    ndeferred = 0;
}

static void fixup_ritem(void) {
    int i;
    for (i = 0; i < nitems; i++) {
        if (ritem[i] > 0) {
            bucket *bp = temp_sym_map[ritem[i]];
            if (bp) {
                ritem[i] = bp->index;
            }
        }
    }
}

static char *scan_action(void) {
    int c;
    int depth = 1;
    size_t size = 128;
    size_t len = 0;
    char *buf = malloc(size);
    if (!buf) no_space();
    
    while (depth > 0) {
        c = nextc();
        if (c == EOF) {
            fprintf(stderr, "EOF in semantic action at line %d, depth=%d\n", lineno, depth);
            done(1);
        }
        if (c == '{') depth++;
        if (c == '}') depth--;
        
        if (depth == 0) {
            buf[len] = '\0';
            return buf;
        }
        
        if (len + 2 >= size) {
            size *= 2;
            buf = realloc(buf, size);
            if (!buf) no_space();
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return buf;
}

static void write_action(int rule, char *body, int offset) {
    char *p = body;
    int c;

    fprintf(action_file, "case %d:\n", rule);
    fprintf(action_file, "{");

    while ((c = *p++) != '\0') {
        if (c == '$') {
            c = *p++;
            if (c == '$') {
                /* $$ -> yyval */
                fprintf(action_file, "yyval");
            } else if (c == '-' || isdigit(c)) {
                /* $N or $-N -> yyvsp[N - offset] */
                int neg = 0;
                int n = 0;
                if (c == '-') {
                    neg = 1;
                    c = *p++;
                }
                while (isdigit(c)) {
                    n = n * 10 + (c - '0');
                    c = *p++;
                }
                p--; /* Unget char */
                if (neg) n = -n;

                fprintf(action_file, "yyvsp[%d]", n - offset);
            } else if (c == '<') {
                /* $<type>N */
                while (c != '>' && c != '\0') c = *p++;
                c = *p++;
                /* Parse N */
                int n = 0;
                while (isdigit(c)) {
                    n = n * 10 + (c - '0');
                    c = *p++;
                }
                p--;
                fprintf(action_file, "yyvsp[%d]", n - offset);
            } else {
                putc('$', action_file);
                putc(c, action_file);
            }
        } else {
            putc(c, action_file);
        }
    }

    fprintf(action_file, "}\nbreak;\n");
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
            for (;;) {
                c = nextc();
                if (c == EOF) { fprintf(stderr, "EOF in %%{ block\n"); done(1); }
                if (c == '%') {
                    int next = nextc();
                    if (next == '}') break;
                    /* Not end marker, write % and continue */
                    if (code_file) putc('%', code_file);
                    if (code_file) putc(next, code_file);
                } else {
                    if (code_file) putc(c, code_file);
                }
            }
            if (code_file) putc('\n', code_file);  /* Add newline after block */
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
                /* Parse token declarations - save directive type first */
                {
                    int decl_type = t;
                    static int prec_level = 0;
                    char *current_tag = NULL;
                    
                    /* Precedence increases with each associativity line */
                    if (decl_type != TERM) prec_level++;
                    
                    for (;;) {
                        t = get_token();
                        
                        /* Check for <tag> */
                        if (t == '<') {
                            /* Read type tag */
                            char tag_buf[MAXTOKEN];
                            int i = 0;
                            int c;
                            while ((c = nextc()) != EOF && c != '>') {
                                if (i < MAXTOKEN - 1) tag_buf[i++] = c;
                            }
                            tag_buf[i] = '\0';
                            current_tag = strdup(tag_buf);
                            continue;
                        }
                        
                        if (t == IDENTIFIER) {
                            bp = lookup(token_buffer);
                            bp->class = CLASS_TERM;
                            
                            /* Set associativity based on saved directive type */
                            if (decl_type == LEFT) bp->assoc = LEFT_ASSOC;
                            else if (decl_type == RIGHT) bp->assoc = RIGHT_ASSOC;
                            else if (decl_type == NONASSOC) bp->assoc = NON_ASSOC;
                            else bp->assoc = NO_ASSOC;
                            
                            /* Set precedence for associativity declarations */
                            if (decl_type != TERM) bp->prec = prec_level;
                            
                            /* Set type tag if present */
                            if (current_tag) bp->tag = current_tag;
                        } else if (t == ',') {
                            continue;
                        } else {
                            /* End of this declaration line */
                            break;
                        }
                    }
                    current_tag = NULL;
                    if (t == MARK) return;
                }
                continue; 
                
            case START:
                t = get_token();
                if (t == IDENTIFIER) {
                     goal_symbol = lookup(token_buffer);
                }
                break;
            
            case UNION:
                /* %union { body } - copy body to union_file */
                {
                    int c;
                    int depth = 0;
                    
                    /* Skip whitespace to find opening brace */
                    while ((c = nextc()) != EOF && c != '{') {
                        if (!isspace(c)) {
                            fprintf(stderr, "yacc: expected '{' after %%union\n");
                            done(1);
                        }
                    }
                    
                    if (c == EOF) {
                        fprintf(stderr, "yacc: unexpected EOF in %%union\n");
                        done(1);
                    }
                    
                    /* c is now '{' */
                    depth = 1;
                    
                    while (depth > 0 && (c = nextc()) != EOF) {
                        if (c == '{') {
                            depth++;
                        } else if (c == '}') {
                            depth--;
                            if (depth == 0) break;
                        } else if (c == '/') {
                            /* Handle comments */
                            int next = nextc();
                            if (next == '*') {
                                putc('/', union_file);
                                putc('*', union_file);
                                /* Skip until */ 
                                int prev = 0;
                                while ((c = nextc()) != EOF) {
                                    putc(c, union_file);
                                    if (prev == '*' && c == '/') break;
                                    prev = c;
                                }
                                continue;
                            } else {
                                ungetc_char(next);
                            }
                        } else if (c == '\'' || c == '"') {
                            /* Handle string/char literals */
                            int quote = c;
                            putc(c, union_file);
                            while ((c = nextc()) != EOF && c != quote) {
                                putc(c, union_file);
                                if (c == '\\') {
                                    c = nextc();
                                    if (c != EOF) putc(c, union_file);
                                }
                            }
                            if (c != EOF) putc(c, union_file);
                            continue;
                        }
                        
                        if (depth > 0) {
                            putc(c, union_file);
                        }
                    }
                    
                    union_defined = 1;
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
                     ritem[nitems++] = -nrules;

                     flush_deferred();

                     nrules++;
                     
                     if (nrules >= MAXPROD) no_space();
                     plhs[nrules] = lhs->index;
                     rrhs[nrules] = nitems;
                } else if (t == ';') {
                     /* End of rule block */
                     if (nitems >= MAXPROD * 4) no_space();
                     ritem[nitems++] = -nrules;

                     flush_deferred();

                     nrules++;
                     break;
                } else if (t == IDENTIFIER) {
                    /* Symbol in RHS */
                    bucket *bp = lookup(token_buffer);
                    if (nitems >= MAXPROD * 4) no_space();
                    ritem[nitems++] = bp->index;
                } else if (t == LCURLY) {
                    /* Semantic action */
                    char *action_body = scan_action();
                    
                    /* Now peek at next token to see if this was mid-rule */
                    int next_t = get_token();
                    if (next_t == '|' || next_t == ';' || next_t == MARK || next_t == 0) {
                        /* End-of-rule action */
                        write_action(nrules, action_body, 0);
                    } else {
                        /* Mid-rule action */
                        /* 1. Create anonymous nonterminal */
                        char name[32];
                        bucket *sym;
                        int mid_rule_idx;
                        int offset;

                        snprintf(name, sizeof(name), "$@%d", ++gen_sym_count);
                        sym = lookup(name);
                        sym->class = CLASS_NONTERM;

                        /* Assign temporary index */
                        if (sym->index == 0) {
                            if (temp_sym_count >= MAXSYM - 1) no_space();
                            sym->index = ++temp_sym_count;
                            temp_sym_map[temp_sym_count] = sym;
                        }

                        /* 2. Add to current rule RHS */
                        if (nitems >= MAXPROD * 4) no_space();
                        ritem[nitems++] = sym->index;

                        /* 3. Create epsilon rule for it */
                        /* Steal nrules */
                        mid_rule_idx = nrules;
                        nrules++;
                        if (nrules >= MAXPROD) no_space();

                        /* Move current rule metadata to nrules */
                        plhs[nrules] = plhs[mid_rule_idx];
                        rrhs[nrules] = rrhs[mid_rule_idx];
                        /* rlhs[nrules] = rlhs[mid_rule_idx]; -- rlhs unused? */

                        /* Set up mid-rule metadata */
                        plhs[mid_rule_idx] = sym->index;
                        /* rrhs[mid_rule_idx] will be set in flush_deferred */

                        /* Calculate offset: items on stack (excluding $$n) */
                        /* nitems points after $$n. rrhs[nrules] points to start. */
                        offset = nitems - 1 - rrhs[nrules];

                        /* Write action for epsilon rule */
                        write_action(mid_rule_idx, action_body, offset);

                        /* Queue epsilon rule for deferred addition to ritem */
                        if (ndeferred >= MAXPROD) no_space();
                        deferred_rules[ndeferred++] = mid_rule_idx;
                    }
                    free(action_body);

                    t = next_t;
                    continue; /* already got next token */
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
    fixup_ritem();
    
    /* Copy epilogue (C code after second %%) to epilogue_file */
    if (epilogue_file) {
        /* Flush any remaining characters in the current line buffer */
        if (cptr) {
            while (*cptr) {
                putc(*cptr, epilogue_file);
                cptr++;
            }
            putc('\n', epilogue_file); /* Restore the newline consumed by get_line */
        }
        
        /* Copy remainder of file */
        int c;
        while ((c = fgetc(input_file)) != EOF) {
            putc(c, epilogue_file);
        }
    }
    
    /* Debug output */
    fprintf(stderr, "yacc: %d tokens, %d nonterminals, %d rules\n", 
            ntokens, nvars, nrules);
}
