#include "defs.h"

int doname(struct nameblock *p, int reclevel, TIMETYPE *tval) {
    if (p->done) {
        *tval = p->modtime;
        return 0;
    }
    
    p->done = 1;
    TIMETYPE ptime = exists(p->namep);
    TIMETYPE truedate = 0;
    
    if (dbgflag) printf("doname(%s)\n", p->namep);

    struct lineblock *lp = p->linep;
    
    if (!lp) {
        // No rules. Source file?
        if (ptime == 0) {
            // Try implicit rule .c.o
            char srcname[256];
            size_t nlen = strlen(p->namep);
            if (nlen > 2 && strcmp(p->namep + nlen - 2, ".o") == 0) {
                strcpy(srcname, p->namep);
                strcpy(srcname + nlen - 2, ".c");
                if (exists(srcname)) {
                    struct nameblock *srcnb = srchname(srcname);
                    if (!srcnb) srcnb = makename(srcname);
                    TIMETYPE td;
                    doname(srcnb, reclevel + 1, &td);
                    if (td > truedate) truedate = td;
                    
                    // Execute default CC command
                    char cmd[512];
                    sprintf(cmd, "cc -c %s -o %s", srcname, p->namep);
                    dosys(cmd, 0);
                    *tval = time(NULL);
                    p->done = 2;
                    p->modtime = *tval;
                    return 0;
                }
            }
            if (keepgoing) return 1;
            fatal1("Don't know how to make %s", p->namep);
        }
        *tval = ptime;
        return 0;
    }

    int out_of_date = 0;
    
    for (; lp; lp = lp->nxtlineblock) {
        struct depblock *dp = lp->depp;
        for (; dp; dp = dp->nxtdepblock) {
            TIMETYPE td;
            doname(dp->depname, reclevel + 1, &td);
            if (td > truedate) truedate = td;
            if (ptime < td) out_of_date = 1;
        }
    }
    
    if (ptime == 0 || out_of_date) {
        // Execute commands
        // In a "modern" make, we'd handle -j here.
        for (lp = p->linep; lp; lp = lp->nxtlineblock) {
            struct shblock *sh = lp->shp;
            for (; sh; sh = sh->nxtshblock) {
                docom(sh);
            }
        }
        // Update time by re-statting
        *tval = exists(p->namep);
        if (*tval == 0) *tval = time(NULL); // Fallback
    } else {
        *tval = ptime;
    }
    
    p->done = 2;
    p->modtime = *tval;
    return 0;
}

int docom(struct shblock *q) {
    char cmd[2048];
    subst(q->shbp, cmd);
    return dosys(cmd, 0);
}

int dosys(char *comstring, int nohalt) {
    if (silflag == 0) printf("%s\n", comstring);
    if (noexflag) return 0;
    
    int ret = system(comstring);
    if (ret != 0 && !ignerr && !nohalt) {
        fatal("Command failed");
    }
    return ret;
}
