#include "defs.h"

// Hand-written parser to replace yacc/lex


void parse_line(char *line);

void rddescf(char *descfile) {
    FILE *f;
    if (strcmp(descfile, "-") == 0) f = stdin;
    else f = fopen(descfile, "r");
    
    if (!f) fatal1("Cannot open %s", descfile);

    char buf[2048];
    char *accum = NULL;
    size_t accum_len = 0;

    // Handle line continuations
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') {
            buf[len-1] = 0;
            len--;
        }
        
        if (len > 0 && buf[len-1] == '\\') {
            buf[len-1] = 0;
            len--;
            // Append
            accum = realloc(accum, accum_len + len + 1);
            strcpy(accum + accum_len, buf);
            accum_len += len;
            continue;
        }
        
        if (accum) {
            accum = realloc(accum, accum_len + len + 1);
            strcpy(accum + accum_len, buf);
            parse_line(accum);
            free(accum);
            accum = NULL;
            accum_len = 0;
        } else {
            parse_line(buf);
        }
    }
    if (accum) {
        parse_line(accum);
        free(accum);
    }

    if (f != stdin) fclose(f);
}

static struct nameblock *cur_targets[64];
static int n_targets = 0;

void parse_line(char *line) {
    // Comments
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    
    // Trim trailing whitespace
    size_t len = strlen(line);
    while (len > 0 && isspace(line[len-1])) line[--len] = 0;
    
    if (len == 0) return;

    if (line[0] == '\t') {
        // Command line
        if (n_targets == 0) fatal("Command line outside rule");
        
        struct shblock *sh = ALLOC(shblock);
        sh->shbp = copys(line + 1); // Skip tab
        sh->nxtshblock = NULL;
        
        for (int i = 0; i < n_targets; i++) {
            struct lineblock *lp = cur_targets[i]->linep;
            // Find last lineblock
            while (lp->nxtlineblock) lp = lp->nxtlineblock;
            
            // Append to shp list
            if (lp->shp == NULL) {
                lp->shp = sh;
            } else {
                struct shblock *s = lp->shp;
                while (s->nxtshblock) s = s->nxtshblock;
                s->nxtshblock = sh;
            }
        }
        return;
    }

    // Macro or Rule
    char *eq = strchr(line, '=');
    char *col = strchr(line, ':');
    
    if (eq && (!col || eq < col)) {
        // Macro: NAME = VAL
        *eq = 0;
        char *name = line;
        char *val = eq + 1;
        while (isspace(*val)) val++;
        // Trim name
        size_t nlen = strlen(name);
        while (nlen > 0 && isspace(name[nlen-1])) name[--nlen] = 0;
        
        setvar(name, val);
        return;
    }

    if (col) {
        // Rule: TARGETS : DEPS
        *col = 0;
        char *tgts = line;
        char *deps = col + 1;
        
        // Parse targets
        n_targets = 0;
        char *t = strtok(tgts, " \t");
        while (t) {
            if (n_targets < 64) {
                struct nameblock *nb = srchname(t);
                if (!nb) nb = makename(t);
                cur_targets[n_targets++] = nb;
            }
            t = strtok(NULL, " \t");
        }
        
        if (n_targets == 0) return;

        // Create lineblock
        struct lineblock *lb = ALLOC(lineblock);
        
        // Parse deps
        struct depblock *head_dep = NULL;
        struct depblock *curr_dep = NULL;
        
        // Need to expand macros in deps?
        char exp_deps[2048];
        subst(deps, exp_deps);
        
        char *d = strtok(exp_deps, " \t");
        while (d) {
            struct depblock *db = ALLOC(depblock);
            struct nameblock *dnb = srchname(d);
            if (!dnb) dnb = makename(d);
            
            db->depname = dnb;
            db->nxtdepblock = NULL;
            
            if (!head_dep) head_dep = db;
            else curr_dep->nxtdepblock = db;
            curr_dep = db;
            
            d = strtok(NULL, " \t");
        }
        
        lb->depp = head_dep;
        
        // Attach to targets
        for (int i = 0; i < n_targets; i++) {
            struct nameblock *p = cur_targets[i];
            if (p->linep == NULL) {
                p->linep = lb;
            } else {
                struct lineblock *l = p->linep;
                while (l->nxtlineblock) l = l->nxtlineblock;
                l->nxtlineblock = lb;
            }
        }
        return;
    }
    
    fatal1("Syntax error: %s", line);
}
