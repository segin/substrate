/*
 * dfa.c - NFA to DFA conversion via subset construction
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "dfa.h"
#include "regex.h"
#include "symtab.h"

/* NFA state set for subset construction */
#define MAX_NFA_STATES 4096

struct nfa_set {
    struct nfa_state *states[MAX_NFA_STATES];
    int count;
    int accept_rules[256];
    int accept_count;
    int accept;         /* Lowest accepting rule, or 0 */
};

/* Hash table for DFA state lookup */
#define HASH_SIZE 1024

struct dfa_map_entry {
    struct nfa_set set;
    struct dfa_state *dfa_state;
    struct dfa_map_entry *next;
};

static struct dfa_map_entry *dfa_map[HASH_SIZE];

/* Compute hash of NFA state set */
static unsigned int set_hash(struct nfa_set *s) {
    unsigned int h = 0;
    for (int i = 0; i < s->count; i++) {
        h = h * 31 + (unsigned long)s->states[i];
    }
    return h % HASH_SIZE;
}

/* Compare two NFA sets */
static int set_equal(struct nfa_set *a, struct nfa_set *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++) {
        if (a->states[i] != b->states[i]) return 0;
    }
    return 1;
}

/* Add state to set if not present */
static void set_add(struct nfa_set *s, struct nfa_state *st) {
    for (int i = 0; i < s->count; i++) {
        if (s->states[i] == st) return;
    }
    if (s->count < MAX_NFA_STATES) {
        s->states[s->count++] = st;
        /* Track accepting state */
        if (st->accept > 0) {
            if (s->accept == 0 || st->accept < s->accept) {
                s->accept = st->accept;
            }
            /* Add to all accepting rules if not present */
            bool found = false;
            for (int j = 0; j < s->accept_count; j++) {
                if (s->accept_rules[j] == st->accept) {
                    found = true;
                    break;
                }
            }
            if (!found && s->accept_count < 256) {
                s->accept_rules[s->accept_count++] = st->accept;
            }
        }
    }
}

/* Sort set for consistent hashing */
static int state_cmp(const void *a, const void *b) {
    struct nfa_state *sa = *(struct nfa_state **)a;
    struct nfa_state *sb = *(struct nfa_state **)b;
    return (sa < sb) ? -1 : (sa > sb) ? 1 : 0;
}

static void set_sort(struct nfa_set *s) {
    qsort(s->states, s->count, sizeof(struct nfa_state *), state_cmp);
}

/* Compute epsilon closure of a set */
static void epsilon_closure(struct nfa_set *s) {
    /* Use stack-based approach */
    struct nfa_state *stack[MAX_NFA_STATES];
    int top = 0;
    
    /* Push all current states */
    for (int i = 0; i < s->count; i++) {
        stack[top++] = s->states[i];
    }
    
    while (top > 0) {
        struct nfa_state *st = stack[--top];
        
        if (st->c == EPSILON) {
            if (st->out1) {
                int found = 0;
                for (int i = 0; i < s->count; i++) {
                    if (s->states[i] == st->out1) { found = 1; break; }
                }
                if (!found) {
                    set_add(s, st->out1);
                    stack[top++] = st->out1;
                }
            }
            if (st->out2) {
                int found = 0;
                for (int i = 0; i < s->count; i++) {
                    if (s->states[i] == st->out2) { found = 1; break; }
                }
                if (!found) {
                    set_add(s, st->out2);
                    stack[top++] = st->out2;
                }
            }
        }
    }
    
    set_sort(s);
}

/* Move: compute states reachable from set on input c */
static void nfa_move(struct nfa_set *from, int c, struct nfa_set *to) {
    to->count = 0;
    to->accept = 0;
    to->accept_count = 0;
    
    for (int i = 0; i < from->count; i++) {
        struct nfa_state *st = from->states[i];
        if (st->c == c || (st->c == ANY_CHAR && c != '\n' && c > 0)) {
            if (st->out1) set_add(to, st->out1);
        }
    }
    
    epsilon_closure(to);
}

/* Look up or create DFA state for NFA set */
static struct dfa_state *get_dfa_state(struct dfa *d, struct nfa_set *s) {
    if (s->count == 0) return NULL;
    
    unsigned int h = set_hash(s);
    
    /* Check existing */
    for (struct dfa_map_entry *e = dfa_map[h]; e; e = e->next) {
        if (set_equal(&e->set, s)) {
            return e->dfa_state;
        }
    }
    
    /* Create new DFA state */
    struct dfa_state *ds = malloc(sizeof(struct dfa_state));
    ds->id = d->num_states++;
    memset(ds->transitions, -1, sizeof(ds->transitions));
    ds->accept = s->accept;
    
    /* Copy multiple accepting rules */
    ds->accept_count = s->accept_count;
    if (ds->accept_count > 0) {
        ds->accept_rules = malloc(ds->accept_count * sizeof(int));
        /* Sort them by priority (ID) for predictable REJECT ordering */
        memcpy(ds->accept_rules, s->accept_rules, ds->accept_count * sizeof(int));
        for (int i = 0; i < ds->accept_count; i++) {
            for (int j = i + 1; j < ds->accept_count; j++) {
                if (ds->accept_rules[i] > ds->accept_rules[j]) {
                    int tmp = ds->accept_rules[i];
                    ds->accept_rules[i] = ds->accept_rules[j];
                    ds->accept_rules[j] = tmp;
                }
            }
        }
    } else {
        ds->accept_rules = NULL;
    }

    ds->next = d->states;
    d->states = ds;
    
    /* Add to map */
    struct dfa_map_entry *e = malloc(sizeof(struct dfa_map_entry));
    memcpy(&e->set, s, sizeof(struct nfa_set));
    e->dfa_state = ds;
    e->next = dfa_map[h];
    dfa_map[h] = e;
    
    return ds;
}

/* Minimize DFA using Hopcroft's algorithm (simplified) */
static void dfa_minimize(struct dfa *d) {
    if (d->num_states <= 1) return;
    
    int *groups = malloc(d->num_states * sizeof(int));
    int num_groups = 0;
    
    struct dfa_state **by_id = malloc(d->num_states * sizeof(struct dfa_state *));
    for (struct dfa_state *s = d->states; s; s = s->next) by_id[s->id] = s;
    
    /* 1. Initial partition by acceptance */
    int max_accept = 0;
    for (int i = 0; i < d->num_states; i++) if (by_id[i]->accept > max_accept) max_accept = by_id[i]->accept;
    
    int *accept_to_group = malloc((max_accept + 1) * sizeof(int));
    memset(accept_to_group, -1, (max_accept + 1) * sizeof(int));
    
    for (int i = 0; i < d->num_states; i++) {
        int acc = by_id[i]->accept;
        if (acc == 0) {
            groups[i] = 0;
        } else {
            if (accept_to_group[acc] == -1) {
                accept_to_group[acc] = ++num_groups;
            }
            groups[i] = accept_to_group[acc];
        }
    }
    num_groups++;
    free(accept_to_group);
    
    /* 2. Iterative splitting */
    bool changed = true;
    while (changed) {
        changed = false;
        int *new_groups = malloc(d->num_states * sizeof(int));
        int next_group_id = 0;
        
        for (int i = 0; i < d->num_states; i++) {
            int found_group = -1;
            for (int k = 0; k < i; k++) {
                if (groups[k] == groups[i]) {
                    bool match = true;
                    for (int c = 0; c < 256; c++) {
                        int t1 = by_id[i]->transitions[c];
                        int t2 = by_id[k]->transitions[c];
                        int g1 = (t1 == -1) ? -1 : groups[t1];
                        int g2 = (t2 == -1) ? -1 : groups[t2];
                        if (g1 != g2) { match = false; break; }
                    }
                    if (match) { found_group = new_groups[k]; break; }
                }
            }
            if (found_group != -1) new_groups[i] = found_group;
            else new_groups[i] = next_group_id++;
        }
        
        if (next_group_id != num_groups) changed = true;
        memcpy(groups, new_groups, d->num_states * sizeof(int));
        num_groups = next_group_id;
        free(new_groups);
    }
    
    /* 3. Consolidate */
    struct dfa_state *new_list = NULL;
    for (int g = 0; g < num_groups; g++) {
        struct dfa_state *ds = malloc(sizeof(struct dfa_state));
        ds->id = g;
        ds->next = new_list;
        new_list = ds;
        
        int rep = -1;
        for (int i = 0; i < d->num_states; i++) if (groups[i] == g) { rep = i; break; }
        
        ds->accept = by_id[rep]->accept;
        ds->accept_count = by_id[rep]->accept_count;
        if (ds->accept_count > 0) {
            ds->accept_rules = malloc(ds->accept_count * sizeof(int));
            memcpy(ds->accept_rules, by_id[rep]->accept_rules, ds->accept_count * sizeof(int));
        } else {
            ds->accept_rules = NULL;
        }
        /* Transitions mapped to group IDs */
        for (int c = 0; c < 256; c++) {
            int t = by_id[rep]->transitions[c];
            ds->transitions[c] = (t == -1) ? -1 : groups[t];
        }
    }
    
    /* Update start states */
    for (int i = 0; i < d->num_start_states; i++) {
        if (d->start_states[i] != -1) d->start_states[i] = groups[d->start_states[i]];
    }
    
    for (int i = 0; i < d->num_states; i++) {
        if (by_id[i]->accept_rules) free(by_id[i]->accept_rules);
        free(by_id[i]);
    }
    free(by_id);
    d->states = new_list;
    d->num_states = num_groups;
    free(groups);
}

/* Build DFA from all rules and start conditions */
struct dfa *nfa_to_dfa(void) {
    struct rule *rules = get_rules();
    if (!rules) {
        fprintf(stderr, "Error: no rules defined\n");
        return NULL;
    }
    
    int num_sc = get_num_start_conditions();
    struct dfa *d = malloc(sizeof(struct dfa));
    d->states = NULL;
    d->num_states = 0;
    d->num_start_states = num_sc * 2;
    d->start_states = malloc(d->num_start_states * sizeof(int));
    memset(dfa_map, 0, sizeof(dfa_map));
    
    struct dfa_state **worklist = malloc(1024 * sizeof(struct dfa_state *));
    int wl_size = 1024;
    int wl_head = 0, wl_tail = 0;

    const char **sc_names = malloc(num_sc * sizeof(char *));
    sc_names[0] = "INITIAL";
    {
        struct start_condition *curr = get_start_conditions();
        for (int i = num_sc - 1; i >= 1; i--) {
            sc_names[i] = curr->name;
            curr = curr->next;
        }
    }

    for (int i = 0; i < num_sc; i++) {
        for (int bol = 0; bol < 2; bol++) {
            struct nfa_set start_set;
            memset(&start_set, 0, sizeof(struct nfa_set));
            
            const char *sc_name = sc_names[i];
            bool sc_exclusive = false;
            if (i > 0) {
                struct start_condition *sc = find_start_condition(sc_name);
                if (sc) sc_exclusive = sc->exclusive;
            }

            for (struct rule *r = rules; r; r = r->next) {
                bool applies = false;
                if (r->sc_count == 0) {
                    if (i == 0 || !sc_exclusive) applies = true;
                } else {
                    for (int k = 0; k < r->sc_count; k++) {
                        if (strcmp(r->start_conditions[k], sc_name) == 0) { applies = true; break; }
                    }
                }
                
                if (applies) {
                    if (!bol && r->has_bol) {
                        /* Skip BOL rule if we are not at BOL */
                    } else {
                        set_add(&start_set, r->nfa);
                    }
                }
            }

            epsilon_closure(&start_set);
            struct dfa_state *ds = get_dfa_state(d, &start_set);
            if (ds) {
                d->start_states[i * 2 + bol] = ds->id;
                int found = 0;
                for (int m = 0; m < wl_tail; m++) if (worklist[m] == ds) { found = 1; break; }
                if (!found) worklist[wl_tail++] = ds;
            } else d->start_states[i * 2 + bol] = -1;
        }
    }
    
    while (wl_head < wl_tail) {
        struct dfa_state *ds = worklist[wl_head++];
        struct nfa_set *current_set = NULL;
        for (int h = 0; h < HASH_SIZE && !current_set; h++) {
            for (struct dfa_map_entry *e = dfa_map[h]; e; e = e->next) {
                if (e->dfa_state == ds) { current_set = &e->set; break; }
            }
        }
        if (!current_set) continue;
        
        for (int c = 1; c < 256; c++) {
            struct nfa_set next_set;
            nfa_move(current_set, c, &next_set);
            if (next_set.count > 0) {
                struct dfa_state *next_ds = get_dfa_state(d, &next_set);
                ds->transitions[c] = next_ds->id;
                int found = 0;
                for (int m = 0; m < wl_tail; m++) if (worklist[m] == next_ds) { found = 1; break; }
                if (!found) {
                    if (wl_tail >= wl_size) {
                        wl_size *= 2;
                        worklist = realloc(worklist, wl_size * sizeof(struct dfa_state *));
                    }
                    worklist[wl_tail++] = next_ds;
                }
            }
        }
    }
    
    free(worklist);
    for (int i = 0; i < HASH_SIZE; i++) {
        struct dfa_map_entry *e = dfa_map[i];
        while (e) {
            struct dfa_map_entry *next = e->next;
            free(e);
            e = next;
        }
        dfa_map[i] = NULL;
    }
    
    printf("Initial DFA: %d states\n", d->num_states);
    dfa_minimize(d);
    printf("Minimized DFA: %d states, %d start states\n", d->num_states, d->num_start_states);
    return d;
}

void dfa_free(struct dfa *d) {
    if (!d) return;
    struct dfa_state *s = d->states;
    while (s) {
        struct dfa_state *next = s->next;
        free(s);
        s = next;
    }
    if (d->start_states) free(d->start_states);
    free(d);
}
