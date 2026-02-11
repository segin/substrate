#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"


static struct input_stream in;

/* Open next file or stdin */
static void open_next_file(void) {
    if (in.current_fp) {
        fclose(in.current_fp);
        in.current_fp = NULL;
    }

    if (in.current_arg >= in.argc) {
        return; // No more files
    }

    // optind is global in options.h? No, standard `optind` from unistd.h or declared in main context usually. 
    // We passed specific argc/argv offset from main.
    
    char *fname = in.argv[in.current_arg];
    if (strcmp(fname, "-") == 0) {
        in.current_fp = stdin;
    } else {
        in.current_fp = fopen(fname, "r");
        if (!in.current_fp) {
            perror(fname);
            exit(1);
        }
    }
    in.current_arg++;
}

static int next_char(void) {
    if (!in.current_fp) return EOF;
    
    int c = fgetc(in.current_fp);
    if (c == 0) {
        fprintf(stderr, "Error: input is not a text file (contains NUL)\n");
        exit(1);
    }
    if (c == EOF) {
        // Try next file
        open_next_file();
        if (!in.current_fp) return EOF; // Really EOF
        return next_char(); // Recurse for next file
    }
    if (c == '\n') in.line_number++;
    return c;
}

static void unput_char(int c) {
    if (in.current_fp && c != EOF) {
        ungetc(c, in.current_fp);
        if (c == '\n') in.line_number--;
    }
}

void init_parser(int argc, char **argv) {
    in.argc = argc;
    in.argv = argv;
    in.current_arg = 0;
    in.current_fp = NULL;
    in.line_number = 1;
    
    if (argc == 0) {
        // Use stdin if no args (handled by logic below? No, we need to set up)
        in.current_fp = stdin;
    } else {
        open_next_file();
    }
}

#include <ctype.h>
#include "symtab.h"
#include "options.h"

/* Buffer for C code in Definitions section */
static char *def_code_buf = NULL;
static size_t def_code_len = 0;
static size_t def_code_cap = 0;

static void append_def_code(int c) {
    if (def_code_len + 2 > def_code_cap) {
        def_code_cap = (def_code_cap == 0) ? 1024 : def_code_cap * 2;
        def_code_buf = realloc(def_code_buf, def_code_cap);
        if (!def_code_buf) {
            perror("realloc");
            exit(1);
        }
    }
    
    /* Check for trigraphs: ??X */
    if (def_code_len >= 2 && def_code_buf[def_code_len-1] == '?' && def_code_buf[def_code_len-2] == '?') {
        if (strchr("=()/!<>-'", c)) {
            fprintf(stderr, "Warning: C-language trigraph ??%c detected in code block\n", c);
        }
    }

    def_code_buf[def_code_len++] = c;
    def_code_buf[def_code_len] = '\0';
}

char *get_def_code(void) {
    return def_code_buf;
}

/* Buffer for User Subroutines section */
static char *sub_code_buf = NULL;
static size_t sub_code_len = 0;
static size_t sub_code_cap = 0;

static void append_sub_code(int c) {
    if (sub_code_len + 2 > sub_code_cap) {
        sub_code_cap = (sub_code_cap == 0) ? 4096 : sub_code_cap * 2;
        sub_code_buf = realloc(sub_code_buf, sub_code_cap);
        if (!sub_code_buf) {
            perror("realloc");
            exit(1);
        }
    }
    
    /* Check for trigraphs: ??X */
    if (sub_code_len >= 2 && sub_code_buf[sub_code_len-1] == '?' && sub_code_buf[sub_code_len-2] == '?') {
        if (strchr("=()/!<>-'", c)) {
            fprintf(stderr, "Warning: C-language trigraph ??%c detected in user subroutines section\n", c);
        }
    }

    sub_code_buf[sub_code_len++] = c;
    sub_code_buf[sub_code_len] = '\0';
}

char *get_sub_code(void) {
    return sub_code_buf;
}

static void parse_subroutines(void) {
    int c;
    while ((c = next_char()) != EOF) {
        append_sub_code(c);
    }
}



/* Helper to read until a specific delimiter at start of line */
static void read_until_delimiter(void) {
    int c;
    bool at_bol = true;
    while ((c = next_char()) != EOF) {
        if (at_bol && c == '%') {
            int c2 = next_char();
            if (c2 == '}') {
                /* Consume rest of line after %} */
                while ((c = next_char()) != EOF && c != '\n');
                return; /* End of block */
            }
            if (c2 == '%') {
                fprintf(stderr, "Warning: line starting with %%%% inside %%{ ... %%} block is prohibited by POSIX\n");
            }
            append_def_code('%');
            if (c2 != EOF) {
                append_def_code(c2);
                at_bol = (c2 == '\n');
            } else {
                at_bol = false;
            }
        } else {
            append_def_code(c);
            at_bol = (c == '\n');
        }
    }
    fprintf(stderr, "Error: missing %%} delimiter\n");
    exit(1);
}

static void parse_definitions(void) {
    int c;
    char line[1024]; // Simple line buffer for directives/definitions
    int len = 0;
    
    while ((c = next_char()) != EOF) {
        // Check for start of line cases
        
        // Case 1: %% -> End of definitions
        if (c == '%') {
            int c2 = next_char();
            if (c2 == '%') {
                // Determine if this is truly the section delimiter
                // POSIX: "The first %% marks the beginning of the Rules section"
                // It must be at the beginning of a line.
                // We are at beginning of logic (looping char by char, but logic needs to be valid)
                // Wait, this loop is char-by-char. We need to handle line context.
                // Let's assume we are at start of line for now or track column.
                
                // Unput to let caller handle section switch or just return?
                // parse_input expects to handle the switch.
                unput_char(c2);
                unput_char(c);
                return;
            }
            
            // Case 2: %{ -> C code block
            if (c2 == '{') {
                read_until_delimiter();
                continue;
            }
            
            // Case 3: Directives (%s, %x, %pointer, %array, %option...)
            // Read rest of line to parse directive
            unput_char(c2);
            // Fall through to line reader
        }
        
        // Case 4: Indented text -> C Code
        if (c == ' ' || c == '\t') {
            append_def_code(c);
            // Read until newline and append
            while ((c = next_char()) != EOF && c != '\n') {
                append_def_code(c);
            }
            append_def_code('\n');
            continue;
        }
        
        if (c == '\n') continue;
        
        // Case 5: Substitution or Directive or Comment?
        // Read line
        len = 0;
        line[len++] = c;
        while ((c = next_char()) != EOF && c != '\n' && len < 1023) {
            line[len++] = c;
        }
        line[len] = '\0';
        
        // Process line
        if (line[0] == '%') {
            /* Directive */
            char *p = line + 1;
            char *cmd = strtok(p, " \t");
            if (!cmd) continue;
            
            if (strcmp(cmd, "s") == 0 || strcmp(cmd, "x") == 0) {
                bool excl = (strcmp(cmd, "x") == 0);
                char *name;
                while ((name = strtok(NULL, " \t"))) {
                    add_start_condition(name, excl);
                }
            } else if (strcmp(cmd, "array") == 0) {
                opt.use_array = true;
                opt.use_pointer = false;
            } else if (strcmp(cmd, "pointer") == 0) {
                opt.use_pointer = true;
                opt.use_array = false;
            } else if (strchr("pnaeko", cmd[0]) && cmd[1] == '\0') {
                 /* Table sizes */
                 char *val_str = strtok(NULL, " \t");
                 if (val_str) {
                     int val = atoi(val_str);
                     switch (cmd[0]) {
                         case 'p': opt.positions = val; break;
                         case 'n': opt.states = val; break;
                         case 'a': opt.transitions = val; break;
                         case 'e': opt.tree_nodes = val; break;
                         case 'k': opt.classes = val; break;
                         case 'o': opt.output_size = val; break;
                     }
                 }
            }
        } else {
            /* Substitution Definition or Garbage? */
            /* Format: NAME definition... */
            char *p = line;
            char *name_start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) {
                *p = '\0';
                char *val_start = p + 1;
                while (*val_start && isspace((unsigned char)*val_start)) val_start++;
                if (*name_start && *val_start) {
                    add_definition(name_start, val_start);
                }
            }
        }
    }
}

/* Parse the three sections */
#include "regex.h"

/* Read a C action block - handles { } nesting */
static char *read_action(int first_char) {
    char *buf = malloc(4096);
    int len = 0;
    int cap = 4096;
    int brace_depth = 0;
    int c = first_char;
    
    if (c == '{') {
        brace_depth = 1;
        buf[len++] = c;
        while ((c = next_char()) != EOF) {
            if (len + 2 >= cap) {
                cap *= 2;
                char *new_buf = realloc(buf, cap);
                if (!new_buf) {
                    perror("realloc");
                    exit(1);
                }
                buf = new_buf;
            }
            buf[len++] = c;
            if (c == '{') brace_depth++;
            else if (c == '}') {
                brace_depth--;
                if (brace_depth == 0) break;
            }
        }
    } else if (c == '|') {
        /* Chain action - just store the marker */
        buf[len++] = c;
    } else {
        /* Single statement - read to semicolon or newline */
        buf[len++] = c;
        while ((c = next_char()) != EOF && c != '\n') {
            if (len + 2 >= cap) {
                cap *= 2;
                char *new_buf = realloc(buf, cap);
                if (!new_buf) {
                    perror("realloc");
                    exit(1);
                }
                buf = new_buf;
            }
            buf[len++] = c;
        }
    }
    buf[len] = '\0';
    return buf;
}

/* Parse rules section */
static void parse_rules(void) {
    int c;
    char pattern[2048];
    int plen = 0;
    
    while ((c = next_char()) != EOF) {
        /* Check for %% ending rules section */
        if (c == '%') {
            int c2 = next_char();
            if (c2 == '%') {
                printf("Mock: Found delimiter, entering User Subroutines Section\n");
                return;
            }
            unput_char(c2);
        }
        
        /* Skip blank lines */
        if (c == '\n') continue;
        
        /* Skip indented C code at start of rules (yylex local defs) */
        if (c == ' ' || c == '\t') {
            while ((c = next_char()) != EOF && c != '\n');
            continue;
        }
        
        /* Parse start condition prefix <STATE1,STATE2> */
        static char **active_sc = NULL;
        static int active_sc_count = 0;
        static bool in_sc_block = false;

        char **start_conds = NULL;
        int sc_count = 0;
        
        if (c == '<') {
            char sc_buf[256];
            int sc_len = 0;
            while ((c = next_char()) != EOF && c != '>') {
                if (c == ',') {
                    sc_buf[sc_len] = '\0';
                    char **new_conds = realloc(start_conds, (sc_count + 1) * sizeof(char*));
                    if (!new_conds) {
                        perror("realloc");
                        exit(1);
                    }
                    start_conds = new_conds;
                    start_conds[sc_count++] = strdup(sc_buf);
                    sc_len = 0;
                } else {
                    sc_buf[sc_len++] = c;
                }
            }
            if (sc_len > 0) {
                sc_buf[sc_len] = '\0';
                char **new_conds = realloc(start_conds, (sc_count + 1) * sizeof(char*));
                if (!new_conds) {
                    perror("realloc");
                    exit(1);
                }
                start_conds = new_conds;
                start_conds[sc_count++] = strdup(sc_buf);
            }
            c = next_char(); /* Get first char of pattern or { */
            
            /* Check for block start <STATE>{ */
            if (c == '{') {
                active_sc = start_conds;
                active_sc_count = sc_count;
                in_sc_block = true;
                continue;
            }
        } else if (c == '}' && in_sc_block) {
            /* End of start condition block */
            for (int i = 0; i < active_sc_count; i++) free(active_sc[i]);
            free(active_sc);
            active_sc = NULL;
            active_sc_count = 0;
            in_sc_block = false;
            continue;
        }

        /* If we are inside a block and no explicit SC given for this rule, inherit */
        if (sc_count == 0 && in_sc_block) {
            sc_count = active_sc_count;
            start_conds = malloc(sc_count * sizeof(char*));
            for (int i = 0; i < sc_count; i++) start_conds[i] = strdup(active_sc[i]);
        }
        
        /* Read pattern until whitespace */
        plen = 0;
        bool in_quotes = false;
        bool in_brackets = false;
        
        while (c != EOF) {
            if (c == '\\') {
                pattern[plen++] = c;
                c = next_char();
                if (c == EOF) break;
                pattern[plen++] = c;
                c = next_char();
            } else if (c == '"') {
                in_quotes = !in_quotes;
                pattern[plen++] = c;
                c = next_char();
            } else if (c == '[' && !in_quotes) {
                in_brackets = true;
                pattern[plen++] = c;
                c = next_char();
            } else if (c == ']' && !in_quotes) {
                in_brackets = false;
                pattern[plen++] = c;
                c = next_char();
            } else if (!in_quotes && !in_brackets && (c == ' ' || c == '\t')) {
                break;
            } else {
                pattern[plen++] = c;
                c = next_char();
            }
            if (plen >= 2047) {
                fprintf(stderr, "Error: pattern too long\n");
                exit(1);
            }
        }
        pattern[plen] = '\0';
        
        if (plen == 0) continue;
        
        /* Skip whitespace to action */
        while (c == ' ' || c == '\t') c = next_char();
        
        char *action = NULL;
        if (c != '\n' && c != EOF) {
            action = read_action(c);
        }
        
        /* Add the rule */
        printf("Mock: Rule %d: pattern='%s' action='%s'\n", 
               get_rules() ? get_rules()->id + 1 : 1, 
               pattern, action ? action : "(default)");
        add_rule(pattern, action, start_conds, sc_count);
        
        if (action) free(action);
        for (int i = 0; i < sc_count; i++) free(start_conds[i]);
        free(start_conds);
    }
}

void parse_input(void) {
    int c;
    
    /* Section 1: Definitions */
    init_symtab();
    
    printf("Mock: Parsing Definitions Section\n");
    parse_definitions();
    
    /* Check for %% to transition */
    c = next_char();
    if (c == '%') {
        int c2 = next_char();
        if (c2 == '%') {
             printf("Mock: Found delimiter, entering Rules Section\n");
             /* Consume newline after %% if present */
             while ((c = next_char()) != EOF && c != '\n');
             
             /* Parse Rules */
             parse_rules();
             
             /* Parse Subroutines */
             parse_subroutines();
        } else {
             fprintf(stderr, "Error: expected marking of rules section\n");
             exit(1);
        }
    } else {
         fprintf(stderr, "Error: expected marking of rules section\n");
         exit(1);
    }
    
    /* Dump definitions for verification */
    if (def_code_buf) {
        printf("Mock: Captured Code Block:\n%s\n", def_code_buf);
    }
    print_symtab();
    
    /* Print rules */
    struct rule *r = get_rules();
    printf("\nCompiled Rules:\n");
    while (r) {
        printf("  Rule %d: %s\n", r->id, r->pattern);
        r = r->next;
    }
}

