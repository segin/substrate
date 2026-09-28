/*
 * codegen.c - write the DFA, the rule actions and the runtime as lex.yy.c.
 *
 * The fixed part of the scanner lives in scanner.skel (compiled into
 * skel.h at build time); this file writes its sections in order,
 * interleaved with the parts that depend on the input.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "options.h"
#include "rules.h"
#include "skel.h"
#include "symtab.h"
#include "util.h"

static void emit_skel(FILE *out, const char *const *lines) {
    for (; *lines != NULL; lines++)
        fprintf(out, "%s\n", *lines);
}

/* Emit a comma-separated int array, 16 values to a line. */
static void emit_int_list(FILE *out, const int *v, int n) {
    for (int i = 0; i < n; i++) {
        fprintf(out, "%d", v[i]);
        if (i < n - 1)
            fprintf(out, ", ");
        if ((i + 1) % 16 == 0)
            fprintf(out, "\n  ");
    }
}

/* #define NAME n for each declared start condition, in declaration order. */
static void emit_start_conditions(FILE *out) {
    int count = get_num_start_conditions();
    const char **names = xmalloc(count * sizeof(char *));
    struct start_condition *curr = get_start_conditions();

    /* The list is newest-first; index 0 is INITIAL. */
    for (int i = count - 1; i >= 1; i--) {
        names[i] = curr->name;
        curr = curr->next;
    }
    for (int i = 1; i < count; i++)
        fprintf(out, "#define %s %d\n", names[i], i);
    free(names);
}

static void emit_tables(FILE *out, struct dfa *d) {
    struct dfa_state **by_id = xmalloc(d->num_states * sizeof(struct dfa_state *));
    int *offsets = xmalloc(d->num_states * sizeof(int));
    int *counts = xmalloc(d->num_states * sizeof(int));
    int current_idx = 0;

    for (struct dfa_state *s = d->states; s; s = s->next)
        by_id[s->id] = s;

    /* Start state for each (start condition, at-beginning-of-line) pair */
    fprintf(out, "static const int yy_start_state[] = {\n  ");
    for (int i = 0; i < d->num_start_states; i++) {
        fprintf(out, "%d", d->start_states[i]);
        if (i < d->num_start_states - 1)
            fprintf(out, ", ");
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "/* DFA transition table */\n");
    fprintf(out, "#define YY_NUM_STATES %d\n\n", d->num_states);

    fprintf(out, "static const short yy_nxt[YY_NUM_STATES][256] = {\n");
    for (int i = 0; i < d->num_states; i++) {
        struct dfa_state *s = by_id[i];

        fprintf(out, "  { /* state %d */\n    ", i);
        for (int c = 0; c < 256; c++) {
            fprintf(out, "%d", s ? s->transitions[c] : -1);
            if (c < 255)
                fprintf(out, ",");
            if ((c + 1) % 16 == 0)
                fprintf(out, "\n    ");
        }
        fprintf(out, "\n  }%s\n", (i < d->num_states - 1) ? "," : "");
    }
    fprintf(out, "};\n\n");

    /*
     * Every rule a state accepts, highest priority last, so REJECT can
     * fall back to the next one: state i's rules are
     * yy_accept_rules[yy_accept_idx[i] .. + yy_accept_cnt[i]].
     */
    fprintf(out, "static const int yy_accept_rules[] = {\n  ");
    for (int i = 0; i < d->num_states; i++) {
        offsets[i] = current_idx;
        counts[i] = by_id[i]->accept_count;
        if (by_id[i]->accept_count > 0) {
            for (int k = 0; k < by_id[i]->accept_count; k++) {
                fprintf(out, "%d, ", by_id[i]->accept_rules[k]);
                current_idx++;
            }
            if (current_idx % 16 == 0)
                fprintf(out, "\n  ");
        }
    }
    fprintf(out, "0\n};\n\n");

    fprintf(out, "static const int yy_accept_idx[] = {\n  ");
    emit_int_list(out, offsets, d->num_states);
    fprintf(out, "\n};\n\n");

    fprintf(out, "static const int yy_accept_cnt[] = {\n  ");
    emit_int_list(out, counts, d->num_states);
    fprintf(out, "\n};\n\n");

    free(counts);
    free(offsets);
    free(by_id);
}

/* yy_do_action(): run a rule's action; returns 1 if it did REJECT. */
static void emit_actions(FILE *out) {
    fprintf(out, "/* Actions */\n");
    fprintf(out, "int yy_do_action(int rule) {\n");
    fprintf(out, "  #undef REJECT\n");
    fprintf(out, "  #define REJECT return 1\n");
    fprintf(out, "  switch (rule) {\n");

    for (struct rule *r = get_rules(); r; r = r->next) {
        fprintf(out, "    case %d:\n", r->id);
        if (r->action && r->action[0]) {
            if (r->action[0] == '|')
                fprintf(out, "      /* fall through */\n");
            else
                fprintf(out, "      %s\n", r->action);
        } else {
            fprintf(out, "      ECHO;\n");
        }
        fprintf(out, "      break;\n");
    }

    fprintf(out, "    default: ECHO; break;\n");
    fprintf(out, "  }\n");
    fprintf(out, "  return 0;\n");
    fprintf(out, "}\n\n");
}

void generate_scanner(struct dfa *d, const char *def_code, const char *sub_code, int to_stdout) {
    FILE *out;

    if (to_stdout) {
        out = stdout;
    } else {
        out = fopen("lex.yy.c", "w");
        if (!out) {
            perror("lex.yy.c");
            exit(1);
        }
    }

    emit_skel(out, skel_prologue);
    emit_start_conditions(out);
    fprintf(out, "\n");

    if (def_code && *def_code) {
        fprintf(out, "/* User code from definitions section */\n");
        fprintf(out, "%s\n", def_code);
    }

    emit_skel(out, skel_runtime_head);
    if (opt.use_array) {
        fprintf(out, "char yytext[YY_BUF_SIZE];\n");
        fprintf(out, "#ifndef YYLMAX\n#define YYLMAX YY_BUF_SIZE\n#endif\n");
    } else {
        fprintf(out, "char *yytext;\n");
    }
    emit_skel(out, skel_runtime);

    emit_tables(out, d);
    emit_actions(out);

    emit_skel(out, skel_yylex_head);
    if (opt.use_array) {
        fprintf(out, "            if (yyleng < YY_BUF_SIZE) {\n");
        fprintf(out, "                memcpy(yytext, yy_bp, yyleng);\n");
        fprintf(out, "                yytext[yyleng] = '\\0';\n");
        fprintf(out, "            }\n");
    } else {
        fprintf(out, "            yytext = yy_bp;\n");
    }
    emit_skel(out, skel_yylex_tail);

    if (sub_code && *sub_code) {
        fprintf(out, "\n/* User subroutines */\n");
        fprintf(out, "%s\n", sub_code);
    }

    if (!to_stdout) {
        fclose(out);
        printf("Generated lex.yy.c\n");
    }
}
