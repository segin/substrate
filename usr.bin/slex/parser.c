/*
 * parser.c - read a lex source: definitions, rules and user subroutines.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"
#include "parser.h"
#include "rules.h"
#include "symtab.h"
#include "util.h"

/* The input files, read one after another as a single stream. */
static struct {
    int argc;
    char **argv;
    int current_arg;
    FILE *current_fp;
    int line_number;
} in;

/* Open next file or stdin */
static void open_next_file(void) {
    if (in.current_fp) {
        fclose(in.current_fp);
        in.current_fp = NULL;
    }

    if (in.current_arg >= in.argc)
        return; /* No more files */

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
    if (!in.current_fp)
        return EOF;

    int c = fgetc(in.current_fp);
    if (c == 0) {
        fprintf(stderr, "Error: input is not a text file (contains NUL)\n");
        exit(1);
    }
    if (c == EOF) {
        /* Carry on with the next file, if any. */
        open_next_file();
        if (!in.current_fp)
            return EOF;
        return next_char();
    }
    if (c == '\n')
        in.line_number++;
    return c;
}

static void unput_char(int c) {
    if (in.current_fp && c != EOF) {
        ungetc(c, in.current_fp);
        if (c == '\n')
            in.line_number--;
    }
}

void init_parser(int argc, char **argv) {
    in.argc = argc;
    in.argv = argv;
    in.current_arg = 0;
    in.current_fp = NULL;
    in.line_number = 1;

    if (argc == 0)
        in.current_fp = stdin;
    else
        open_next_file();
}

/* C code copied through from the definitions and subroutines sections. */
static struct strbuf def_code;
static struct strbuf sub_code;

/*
 * Append c to a code buffer, warning when it completes a C trigraph:
 * POSIX lex does not translate them, so the C compiler would.
 */
static void append_code(struct strbuf *sb, int c, const char *where) {
    if (sb->len >= 2 && sb->buf[sb->len - 1] == '?' &&
        sb->buf[sb->len - 2] == '?' && strchr("=()/!<>-'", c))
        fprintf(stderr, "Warning: C-language trigraph ??%c detected in %s\n",
                c, where);
    strbuf_putc(sb, c);
}

static void append_def_code(int c) {
    append_code(&def_code, c, "code block");
}

static void append_sub_code(int c) {
    append_code(&sub_code, c, "user subroutines section");
}

char *get_def_code(void) {
    return def_code.buf;
}

char *get_sub_code(void) {
    return sub_code.buf;
}

static void parse_subroutines(void) {
    int c;

    while ((c = next_char()) != EOF)
        append_sub_code(c);
}

/* Copy a %{ ... %} block into the definitions code, up to the %}. */
static void read_until_delimiter(void) {
    int c;
    bool at_bol = true;

    while ((c = next_char()) != EOF) {
        if (at_bol && c == '%') {
            int c2 = next_char();
            if (c2 == '}') {
                /* Consume rest of line after %} */
                while ((c = next_char()) != EOF && c != '\n')
                    ;
                return;
            }
            if (c2 == '%')
                fprintf(stderr, "Warning: line starting with %%%% inside %%{ ... %%} block is prohibited by POSIX\n");
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

/* A %-directive line from the definitions section (without the %). */
static void parse_directive(char *p) {
    char *cmd = strtok(p, " \t");

    if (!cmd)
        return;

    if (strcmp(cmd, "s") == 0 || strcmp(cmd, "x") == 0) {
        bool excl = (strcmp(cmd, "x") == 0);
        char *name;

        while ((name = strtok(NULL, " \t")))
            add_start_condition(name, excl);
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
}

/* A "name definition" line from the definitions section. */
static void parse_substitution(char *line) {
    char *p = line;
    char *name_start = p;

    while (*p && !isspace((unsigned char)*p))
        p++;
    if (*p) {
        *p = '\0';
        char *val_start = p + 1;
        while (*val_start && isspace((unsigned char)*val_start))
            val_start++;
        if (*name_start && *val_start)
            add_definition(name_start, val_start);
    }
}

static void parse_definitions(void) {
    int c;
    char line[1024];
    int len = 0;

    while ((c = next_char()) != EOF) {
        if (c == '%') {
            int c2 = next_char();

            /* %% ends the section; leave it for parse_input. */
            if (c2 == '%') {
                unput_char(c2);
                unput_char(c);
                return;
            }

            /* %{ starts a C code block. */
            if (c2 == '{') {
                read_until_delimiter();
                continue;
            }

            /* Any other % starts a directive: read it as a line below. */
            unput_char(c2);
        }

        /* An indented line is C code. */
        if (c == ' ' || c == '\t') {
            append_def_code(c);
            while ((c = next_char()) != EOF && c != '\n')
                append_def_code(c);
            append_def_code('\n');
            continue;
        }

        if (c == '\n')
            continue;

        len = 0;
        line[len++] = c;
        while ((c = next_char()) != EOF && c != '\n' && len < 1023)
            line[len++] = c;
        line[len] = '\0';

        if (line[0] == '%')
            parse_directive(line + 1);
        else
            parse_substitution(line);
    }
}

/* Read a C action block - handles { } nesting */
static char *read_action(int first_char) {
    char *buf = xmalloc(4096);
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
                buf = xrealloc(buf, cap);
            }
            buf[len++] = c;
            if (c == '{') {
                brace_depth++;
            } else if (c == '}') {
                brace_depth--;
                if (brace_depth == 0)
                    break;
            }
        }
    } else if (c == '|') {
        /* Chain action - just store the marker */
        buf[len++] = c;
    } else {
        /* Single statement - read to the end of the line */
        buf[len++] = c;
        while ((c = next_char()) != EOF && c != '\n') {
            if (len + 2 >= cap) {
                cap *= 2;
                buf = xrealloc(buf, cap);
            }
            buf[len++] = c;
        }
    }
    buf[len] = '\0';
    return buf;
}

/* Append a copy of name to a growable start-condition list. */
static void sc_list_add(char ***list, int *count, int *cap, const char *name) {
    if (*count >= *cap) {
        *cap = *cap == 0 ? 8 : *cap * 2;
        *list = xrealloc(*list, *cap * sizeof(char *));
    }
    (*list)[(*count)++] = xstrdup(name);
}

/* Parse rules section */
static void parse_rules(void) {
    int c;
    char pattern[2048];
    int plen = 0;

    /* The enclosing <SC>{ ... } block, if any. */
    static char **active_sc = NULL;
    static int active_sc_count = 0;
    static bool in_sc_block = false;

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
        if (c == '\n')
            continue;

        /* Skip indented C code at start of rules (yylex local defs) */
        if (c == ' ' || c == '\t') {
            while ((c = next_char()) != EOF && c != '\n')
                ;
            continue;
        }

        /* Parse start condition prefix <STATE1,STATE2> */
        char **start_conds = NULL;
        int sc_count = 0;
        int sc_cap = 0;

        if (c == '<') {
            char sc_buf[256];
            int sc_len = 0;

            while ((c = next_char()) != EOF && c != '>') {
                if (c == ',') {
                    sc_buf[sc_len] = '\0';
                    sc_list_add(&start_conds, &sc_count, &sc_cap, sc_buf);
                    sc_len = 0;
                } else {
                    sc_buf[sc_len++] = c;
                }
            }
            if (sc_len > 0) {
                sc_buf[sc_len] = '\0';
                sc_list_add(&start_conds, &sc_count, &sc_cap, sc_buf);
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
            for (int i = 0; i < active_sc_count; i++)
                free(active_sc[i]);
            free(active_sc);
            active_sc = NULL;
            active_sc_count = 0;
            in_sc_block = false;
            continue;
        }

        /* If we are inside a block and no explicit SC given for this rule, inherit */
        if (sc_count == 0 && in_sc_block) {
            sc_count = active_sc_count;
            start_conds = xmalloc(sc_count * sizeof(char *));
            for (int i = 0; i < sc_count; i++)
                start_conds[i] = xstrdup(active_sc[i]);
        }

        /* Read pattern until whitespace */
        plen = 0;
        bool in_quotes = false;
        bool in_brackets = false;

        while (c != EOF) {
            if (c == '\\') {
                pattern[plen++] = c;
                c = next_char();
                if (c == EOF)
                    break;
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

        if (plen == 0)
            continue;

        /* Skip whitespace to action */
        while (c == ' ' || c == '\t')
            c = next_char();

        char *action = NULL;
        if (c != '\n' && c != EOF)
            action = read_action(c);

        printf("Mock: Rule %d: pattern='%s' action='%s'\n",
               get_rules() ? get_rules()->id + 1 : 1,
               pattern, action ? action : "(default)");
        add_rule(pattern, action, start_conds, sc_count);

        free(action);
        for (int i = 0; i < sc_count; i++)
            free(start_conds[i]);
        free(start_conds);
    }
}

void parse_input(void) {
    int c;

    /* Section 1: Definitions */
    init_symtab();

    printf("Mock: Parsing Definitions Section\n");
    parse_definitions();

    /* The first %% is required. */
    c = next_char();
    if (c != '%' || next_char() != '%') {
        fprintf(stderr, "Error: expected marking of rules section\n");
        exit(1);
    }
    printf("Mock: Found delimiter, entering Rules Section\n");
    /* Consume newline after %% if present */
    while ((c = next_char()) != EOF && c != '\n')
        ;

    parse_rules();
    parse_subroutines();

    /* Dump definitions for verification */
    if (def_code.buf)
        printf("Mock: Captured Code Block:\n%s\n", def_code.buf);
    print_symtab();

    printf("\nCompiled Rules:\n");
    for (struct rule *r = get_rules(); r; r = r->next)
        printf("  Rule %d: %s\n", r->id, r->pattern);
}
