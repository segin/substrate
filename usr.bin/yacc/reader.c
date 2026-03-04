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

/* Temporary symbol mapping for ritem fixup */
static bucket *temp_sym_map[MAXSYM];
static int temp_sym_count = 0;

/* Mid-rule action generation */
static int gen_sym_count = 0;

/* Forward declarations */
static void get_line(void);
static int nextc(void);
static void ungetc_char(int c);
static int keyword(void);
static void skip_comment(void);
static void create_symbol_table(void);
static char *scan_action(void);
static void write_action(int rule, char *body, int offset, int context_base);
static void fixup_grammar(void);
static int get_token(void);
static bucket *make_dummy(void);

/* ... */

/* Track current rule's RHS length for $N translation */

static bucket *make_dummy(void) {
    char name[32];
    snprintf(name, sizeof(name), "$@%d", ++gen_sym_count);
    /*
     * Mid-rule action symbols must be interned in the global symbol table
     * so pack_symbols()/verbose output can resolve stable final indices.
     */
    bucket *bp = lookup(name);
    bp->class = CLASS_NONTERM;
    
    /* Assign temp index */
    if (temp_sym_count >= MAXSYM - 1) no_space();
    bp->index = ++temp_sym_count;
    temp_sym_map[temp_sym_count] = bp;
    
    return bp;
}

static char *scan_action(void) {
    int c;
    int depth = 1;
    int len = 0;
    int size = 1024;
    char *buf = malloc(size);
    if (!buf) no_space();
    
    while (depth > 0) {
        c = nextc();
        if (c == EOF) {
            fprintf(stderr, "EOF in semantic action at line %d\n", lineno);
            done(1);
        }

        if (c == '{') depth++;
        if (c == '}') depth--;
        
        if (depth == 0) {
            break;
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

static void write_action(int rule, char *body, int offset, int context_base) {
    char *p = body;
    int c;

    fprintf(action_file, "case %d:\n", rule);
    fprintf(action_file, "{");

    while ((c = *p++) != '\0') {
        if (c == '$') {
            c = *p++;
            if (c == '<') {
                 /* Typed access $<type>... */
                 char tag[MAXTOKEN];
                 int i = 0;

                 /* Read tag until > */
                 while ((c = *p++) != '>' && c != '\0') {
                     if (i < MAXTOKEN - 1) tag[i++] = c;
                 }
                 tag[i] = '\0';

                 if (c == '\0') {
                     /* Unterminated tag, should not happen if scan_action correct */
                     break;
                 }

                 c = *p++;
                 if (c == '$') {
                     /* $<tag>$ -> yyval.tag */
                     fprintf(action_file, "yyval.%s", tag);
                 } else if (c == '-' || isdigit(c)) {
                     /* $<tag>N -> yyvsp[...].tag */
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
                     p--;
                     if (neg) n = -n;
                     fprintf(action_file, "yyvsp[%d].%s", n - offset, tag);
                 } else {
                     /* Unknown $<tag>... construct, pass through */
                     putc('$', action_file);
                     putc('<', action_file);
                     fputs(tag, action_file);
                     putc('>', action_file);
                     putc(c, action_file);
                 }
            } else if (c == '$') {
                /* $$ - lookup LHS type */
                bucket *lhs_bp = NULL;
                if (plhs[rule] > 0) {
                    lhs_bp = temp_sym_map[plhs[rule]];
                }
                if (lhs_bp && lhs_bp->tag) {
                    fprintf(action_file, "yyval.%s", lhs_bp->tag);
                } else {
                    fprintf(action_file, "yyval");
                }
            } else if (c == '-' || isdigit(c)) {
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
                p--;
                if (neg) n = -n;

                /* Typed lookup for $n */
                bucket *sym_bp = NULL;
                if (n > 0) {
                     int sym_idx = ritem[context_base + n - 1];
                     if (sym_idx > 0) {
                         sym_bp = temp_sym_map[sym_idx];
                     }
                }

                if (sym_bp && sym_bp->tag) {
                    fprintf(action_file, "yyvsp[%d].%s", n - offset, sym_bp->tag);
                } else {
                    fprintf(action_file, "yyvsp[%d]", n - offset);
                }
            } else {
                putc('$', action_file);
                if (c) putc(c, action_file);
            }
        } else {
            putc(c, action_file);
        }
    }

    fprintf(action_file, "}\nbreak;\n");
}

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
        if (c == EOF) {
            return 0;
        }
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
        int kw = keyword();
        return kw;
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
    
    if (c == '{') { return LCURLY; }
    if (c == '}') { return RCURLY; }
    
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

    t = get_token();

    for (;;) {
        if (t == MARK) return; /* %% section separator */
        if (t == 0) return;    /* EOF */
        
        switch (t) {
            case TERM:
            case LEFT:
            case RIGHT:
            case NONASSOC:
            case TYPE:
                /* Parse token declarations - save directive type first */
                {
                    int decl_type = t;
                    static int prec_level = 0;
                    char *current_tag = NULL;
                    
                    /* Precedence increases with each associativity line */
                    if (decl_type != TERM && decl_type != TYPE) prec_level++;
                    
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
                            if (decl_type != TYPE) {
                                bp->class = CLASS_TERM;
                            } else {
                                if (bp->class == UNKNOWN) bp->class = CLASS_NONTERM;
                            }
                            
                            /* Set associativity based on saved directive type */
                            if (decl_type == LEFT) bp->assoc = LEFT_ASSOC;
                            else if (decl_type == RIGHT) bp->assoc = RIGHT_ASSOC;
                            else if (decl_type == NONASSOC) bp->assoc = NON_ASSOC;
                            else if (decl_type != TYPE) bp->assoc = NO_ASSOC;
                            
                            /* Set precedence for associativity declarations */
                            if (decl_type != TERM && decl_type != TYPE) bp->prec = prec_level;
                            
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
                }
                continue; 
                
            case START:
                t = get_token();
                if (t == IDENTIFIER) {
                     goal_symbol = lookup(token_buffer);
                }
                t = get_token();
                continue;
            
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
                t = get_token();
                continue;
                
            default:
                /* Unknown or unexpected token */
                /* Could be type decl or just syntax error */
                fprintf(stderr, "syntax error in declarations, t=%d\n", t);
                done(1);
        }
    }
}

static void parse_rules(void) {
    int t;
    int deferred_rules[256]; /* Should be enough for one rule */
    int n_deferred = 0;
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
            
            /* Assign temp index if needed */
            if (lhs->index == 0) {
                if (temp_sym_count >= MAXSYM - 1) no_space();
                lhs->index = ++temp_sym_count;
                temp_sym_map[temp_sym_count] = lhs;
            }

            if (goal_symbol == NULL) goal_symbol = lhs;
            
            t = get_token();
            if (t != ':') {
                 fprintf(stderr, "syntax error: expected ':' after %s\n", lhs->name);
                 done(1);
            }
            
            /* Reset deferred rules for new LHS? No, per RHS. */
            n_deferred = 0;

            /* Parse RHS alternatives */
            for (;;) {
                if (t == ':') { 
                    /* Initial colonization */ 
                    /* Start a new production */
                    if (nrules >= MAXPROD) no_space();
                    plhs[nrules] = lhs->index;
                    rrhs[nrules] = nitems;
                    /* Note: defs.h has plhs, rlhs. Usually plhs is sufficient. rlhs might be per-item? */
                    /* Let's stick to: plhs[rule] = lhs_symbol_index. ritem[item_index] = symbol_index. */
                    /* We need to track where a rule starts in ritem. 'rrhs[rule]' can point to start in ritem. */
                    n_deferred = 0; /* New rule */
                } else if (t == '|') {
                     /* End current production, start new one with same LHS */
                     if (nitems >= MAXPROD * 4) no_space();
                     ritem[nitems++] = -nrules; /* End of rule marker? Or 0? Classic yacc uses negated rule number or similar */
                     nrules++;
                     
                     /* Flush deferred rules for the COMPLETED rule */
                     int i;
                     for (i = 0; i < n_deferred; i++) {
                         int idx = deferred_rules[i];
                         rrhs[idx] = nitems;
                         ritem[nitems++] = -idx; /* Epsilon rule */
                     }
                     n_deferred = 0;

                     if (nrules >= MAXPROD) no_space();
                     plhs[nrules] = lhs->index;
                     rrhs[nrules] = nitems;
                } else if (t == ';') {
                     /* End of rule block */
                     if (nitems >= MAXPROD * 4) no_space();
                     ritem[nitems++] = -nrules;
                     nrules++;

                     /* Flush deferred rules */
                     int i;
                     for (i = 0; i < n_deferred; i++) {
                         int idx = deferred_rules[i];
                         rrhs[idx] = nitems;
                         ritem[nitems++] = -idx;
                     }
                     n_deferred = 0;
                     break;
                } else if (t == IDENTIFIER) {
                    /* Symbol in RHS */
                    bucket *bp = lookup(token_buffer);
                    if (nitems >= MAXPROD * 4) no_space();

                    /* Assign temp index if needed */
                    if (bp->index == 0) {
                         if (temp_sym_count >= MAXSYM - 1) no_space();
                         bp->index = ++temp_sym_count;
                         temp_sym_map[temp_sym_count] = bp;
                    }

                    ritem[nitems++] = bp->index;
                } else if (t == LCURLY) {
                    /* Semantic action */
                    char *body = scan_action();
                    
                    /* Now peek at next token to see if this was mid-rule */
                    int next_t = get_token();
                    if (next_t == '|' || next_t == ';' || next_t == MARK || next_t == 0) {
                        /* End-of-rule action - no special handling needed */
                        write_action(nrules, body, 0, rrhs[nrules]);
                        free(body);
                    } else {
                        /* Mid-rule action - need to create anonymous symbol */
                        /* Create dummy symbol */
                        bucket *dummy = make_dummy();

                        /* Add dummy to current rule */
                        if (nitems >= MAXPROD * 4) no_space();
                        ritem[nitems++] = dummy->index;

                        /* Steal current rule index */
                        int mid_rule_idx = nrules;
                        nrules++;
                        if (nrules >= MAXPROD) no_space();

                        /* Move current rule info to new slot */
                        plhs[nrules] = plhs[mid_rule_idx];
                        rrhs[nrules] = rrhs[mid_rule_idx];

                        /* Setup mid-rule info */
                        plhs[mid_rule_idx] = dummy->index;
                        /* rrhs[mid_rule_idx] deferred */

                        /* Calculate offset: symbols before dummy */
                        /* rrhs[nrules] points to start of current rule (which is at new nrules) */
                        /* nitems points after dummy */
                        /* So items count = (nitems - 1) - rrhs[nrules] */
                        int offset = (nitems - 1) - rrhs[nrules];

                        write_action(mid_rule_idx, body, offset, rrhs[nrules]);
                        free(body);

                        /* Defer rule definition */
                        if (n_deferred < 256) {
                            deferred_rules[n_deferred++] = mid_rule_idx;
                        } else {
                            fprintf(stderr, "Too many mid-rule actions\n");
                            done(1);
                        }
                    }
                    t = next_t;
                    continue; /* already got next token */
                } else if (t == MARK || t == 0) {
                     /* unexpected end of rules? */
                     ritem[nitems++] = -nrules;
                     nrules++;

                     /* Flush deferred */
                     int i;
                     for (i = 0; i < n_deferred; i++) {
                         int idx = deferred_rules[i];
                         rrhs[idx] = nitems;
                         ritem[nitems++] = -idx;
                     }

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
    bucket *end = lookup("$end");
    end->class = CLASS_TERM;
    end->value = 0;
    /* Assign temp index */
    end->index = ++temp_sym_count;
    temp_sym_map[temp_sym_count] = end;

    error_symbol = lookup("error");
    error_symbol->class = CLASS_TERM;
    /* Assign temp index */
    error_symbol->index = ++temp_sym_count;
    temp_sym_map[temp_sym_count] = error_symbol;
}

static void create_augmented_rule(void) {
    bucket *accept_sym = lookup("$accept");
    accept_sym->class = CLASS_NONTERM;
    /* Assign temp index */
    if (accept_sym->index == 0) {
        accept_sym->index = ++temp_sym_count;
        temp_sym_map[temp_sym_count] = accept_sym;
    }

    bucket *end_sym = lookup("$end");

    /* Create Rule 1: $accept : goal $end */
    if (nitems + 3 >= MAXPROD * 4) no_space();

    plhs[1] = accept_sym->index;
    rrhs[1] = nitems;

    ritem[nitems++] = goal_symbol->index;
    ritem[nitems++] = end_sym->index;
    ritem[nitems++] = -1; /* End of Rule 1 */
}

/* Assign indices and count symbols */
static void pack_symbols(void) {
    bucket *bp;
    int token_value = 257;  /* Token values start at 257 */
    
    ntokens = 0;
    nvars = 0;
    nsyms = 0;
    
    /* Assign values to tokens that don't have them */
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM) {
            if (bp->value == 0) {
                 if (strcmp(bp->name, "$end") == 0) bp->value = 0;
                 else if (strcmp(bp->name, "error") == 0) bp->value = 256;
                 else bp->value = token_value++;
            }
        }
    }

    /* Assign indices = values */
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM) {
            bp->index = bp->value;
            if (bp->index >= ntokens) ntokens = bp->index + 1;
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

static void fixup_grammar(void) {
    int i;
    /* Fixup ritem */
    for (i = 0; i < nitems; i++) {
        if (ritem[i] > 0) {
            bucket *bp = temp_sym_map[ritem[i]];
            ritem[i] = bp->index;
        }
    }

    /* Fixup plhs */
    for (i = 0; i < nrules; i++) {
         if (plhs[i] > 0) {
             bucket *bp = temp_sym_map[plhs[i]];
             plhs[i] = bp->index;
         }
    }
}

void reader(void) {
    create_symbol_table();
    get_line(); /* Prime the pump */
    parse_declarations();
    parse_rules();
    create_augmented_rule();
    pack_symbols();
    fixup_grammar();
    
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
