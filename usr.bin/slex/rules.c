/*
 * rules.c - the rules section: anchors, compilation and storage.
 */

#include <stdlib.h>
#include <string.h>

#include "rules.h"
#include "util.h"

static struct rule *rules = NULL;
static int next_rule_id = 1;

void add_rule(const char *pattern, const char *action, char **start_conds,
              int sc_count) {
    struct rule *r = xmalloc(sizeof(struct rule));

    r->id = next_rule_id++;
    r->pattern = xstrdup(pattern);
    r->action = action ? xstrdup(action) : NULL;
    r->start_conditions = xmalloc(sc_count * sizeof(char *));
    for (int i = 0; i < sc_count; i++)
        r->start_conditions[i] = xstrdup(start_conds[i]);
    r->sc_count = sc_count;

    /* Anchors */
    r->has_bol = (pattern[0] == '^');

    const char *p = pattern;
    if (r->has_bol)
        p++;

    /* A trailing unescaped $ is trailing context: r$ becomes r/\n. */
    char *p_modified = xstrdup(p);
    size_t p_len = strlen(p_modified);
    r->has_eol = false;
    if (p_len > 0 && p_modified[p_len - 1] == '$' &&
        (p_len == 1 || p_modified[p_len - 2] != '\\')) {
        char *new_p = xmalloc(p_len + 2);

        r->has_eol = true;
        memcpy(new_p, p_modified, p_len - 1);
        new_p[p_len - 1] = '/';
        new_p[p_len] = '\n';
        new_p[p_len + 1] = '\0';
        free(p_modified);
        p_modified = new_p;
    }

    struct nfa_frag *frag = regex_compile(p_modified, r->id);
    r->nfa = frag->start;
    free(frag->out);
    free(frag);
    free(p_modified);

    r->next = rules;
    rules = r;
}

struct rule *get_rules(void) {
    return rules;
}
