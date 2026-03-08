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

void parse_cmd(char *cmd, char **argv, int max_args) {
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args - 1) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char quote = 0;
        char *token_start = p;
        char *dst = p;

        while (*p) {
            if (*p == '\\' && *(p+1)) {
                p++;
                *dst++ = *p++;
            } else if ((*p == '\'' || *p == '"')) {
                if (!quote) {
                    quote = *p;
                    p++;
                } else if (quote == *p) {
                    quote = 0;
                    p++;
                } else {
                    *dst++ = *p++;
                }
            } else if (isspace((unsigned char)*p) && !quote) {
                p++;
                break;
            } else {
                *dst++ = *p++;
            }
        }
        *dst = '\0';
        argv[argc++] = token_start;
    }
    argv[argc] = NULL;
}

int dosys(char *comstring, int nohalt) {
    if (silflag == 0) printf("%s\n", comstring);
    if (noexflag) return 0;
    
    int pid;
    int status;

    char *cmd_copy = strdup(comstring);
    if (!cmd_copy) {
        fatal("strdup failed");
    }

    char *argv[128];
    parse_cmd(cmd_copy, argv, 128);

    if (argv[0] == NULL) {
        free(cmd_copy);
        return 0;
    }

    if ((pid = fork()) == 0) {
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid < 0) {
        free(cmd_copy);
        if (!ignerr && !nohalt) {
            fatal("fork failed");
        }
        return -1;
    }

    free(cmd_copy);

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }

    int ret = -1;
    if (WIFEXITED(status)) {
        ret = WEXITSTATUS(status);
    }

    if (ret != 0 && !ignerr && !nohalt) {
        fatal("Command failed");
    }
    return ret;
}
