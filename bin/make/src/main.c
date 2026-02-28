#include "defs.h"

struct nameblock *mainname = NULL;
struct nameblock *firstname = NULL;
struct lineblock *sufflist = NULL;
struct pattern *firstpat = NULL;

int ignerr = 0;
int silflag = 0;
int noexflag = 0;
int keepgoing = 0;
int touchflag = 0;
int questflag = 0;
int dbgflag = 0;

int main(int argc, char *argv[]) {
    struct nameblock *p;
    int i, j;
    int descset = 0;
    char c;
    TIMETYPE tjunk;

    // Pre-set some variables
    setvar("MAKE", "make");

    for(i=1; i<argc; ++i) {
        if(argv[i][0] == '-') {
            for(j=1; (c=argv[i][j])!='\0'; ++j) {
                switch(c) {
                    case 'd': dbgflag = YES; break;
                    case 's': silflag = YES; break;
                    case 'i': ignerr = YES; break;
                    case 'k': keepgoing = YES; break;
                    case 'n': noexflag = YES; break;
                    case 't': touchflag = YES; break;
                    case 'q': questflag = YES; break;
                    case 'f':
                        if(i >= argc-1) fatal("No description argument after -f flag");
                        rddescf(argv[++i]);
                        descset = 1;
                        break;
                    default:
                        fprintf(stderr, "Unknown flag: %c\n", c);
                        exit(1);
                }
            }
        } else if (strchr(argv[i], '=')) {
            // Macro definition
            char *eq = strchr(argv[i], '=');
            *eq = 0;
            setvar(argv[i], eq+1);
        }
    }

    if (!descset) {
        if (access("makefile", R_OK) == 0) rddescf("makefile");
        else if (access("Makefile", R_OK) == 0) rddescf("Makefile");
        else fatal("No makefile found");
    }

    if (srchname(".IGNORE")) ignerr = 1;
    if (srchname(".SILENT")) silflag = 1;
    
    p = srchname(".SUFFIXES");
    if (p) sufflist = p->linep;

    int nfargs = 0;
    for(i=1; i<argc; ++i) {
        if (argv[i][0] != '-' && !strchr(argv[i], '=')) {
            p = srchname(argv[i]);
            if (!p) p = makename(argv[i]);
            doname(p, 0, &tjunk);
            nfargs++;
        }
    }

    if (nfargs == 0) {
        if (mainname == NULL) fatal("No targets");
        doname(mainname, 0, &tjunk);
    }

    exit(0);
}

void fatal(char *s) {
    fprintf(stderr, "make: %s\n", s);
    exit(1);
}

void fatal1(char *s, char *a) {
    fprintf(stderr, "make: ");
    fprintf(stderr, s, a);
    fprintf(stderr, "\n");
    exit(1);
}
