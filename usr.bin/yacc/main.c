/*
 * main.c - Main driver for yacc
 *
 * Implements POSIX-compliant argument parsing and pass orchestration.
 */

#include "defs.h"
#include <unistd.h>

/* Global option flags */
int dflag = 0;
int lflag = 0;
int tflag = 0;
int vflag = 0;
int gflag = 0;  /* -g: graph output (extension) */

char *file_prefix = "y";
char *symbol_prefix = "yy";
char *infile_name = NULL;

FILE *input_file = NULL;
FILE *action_file = NULL; /* For storing semantic actions temporally */
FILE *code_file = NULL;   /* For stored code */
FILE *epilogue_file = NULL; /* For epilogue code */
FILE *defines_file = NULL;
FILE *output_file = NULL;
FILE *verbose_file = NULL;
FILE *graph_file = NULL;
FILE *union_file = NULL; /* For storing %union body */

int nitems;
int nrules;
int nsyms;
int ntokens;
int nvars;
int union_defined = 0;

short plhs[MAXPROD];
short ritem[MAXPROD * 4];
short rlhs[MAXPROD]; /* symbol index for LHS of rule i */
short rrhs[MAXPROD]; /* index in ritem for RHS of rule i */

int lineno = 1;
char *temp_name1 = NULL;
char *temp_name2 = NULL;
char *temp_name3 = NULL;

/* Implement get_line to track line numbers for error messages */

static void usage(void) {
    fprintf(stderr, "usage: yacc [-dltv] [-b file_prefix] [-p sym_prefix] filename\n");
    exit(1);
}

void get_args(int argc, char *argv[]) {
    int ch;
    while ((ch = getopt(argc, argv, "b:dglp:tv")) != -1) {
        switch (ch) {
            case 'b':
                file_prefix = optarg;
                break;
            case 'd':
                dflag = 1;
                break;
            case 'g':
                gflag = 1;
                break;
            case 'l':
                lflag = 1;
                break;
            case 'p':
                symbol_prefix = optarg;
                break;
            case 't':
                tflag = 1;
                break;
            case 'v':
                vflag = 1;
                break;
            case '?':
            default:
                usage();
        }
    }

    if (optind == argc) {
        /* POSIX: stdin not supported for grammar file, though classic yacc did.
         * The prompt requires POSIX behavior: one file argument. */
        usage();
    }
    
    infile_name = argv[optind];
}

void open_files(void) {
    input_file = fopen(infile_name, "r");
    if (input_file == NULL) {
        perror(infile_name);
        exit(1);
    }

    /* Create temporary files for actions and code section */
    /* Implementation detail using tmpfile() or mkstemp() mechanism suitable for embedding */
    /* For simplicity in this skeleton, assume we can use tmpfile() but need to check failure */
    action_file = tmpfile();
    code_file = tmpfile();
    epilogue_file = tmpfile();
    union_file = tmpfile();
    
    if (!action_file || !code_file || !epilogue_file || !union_file) {
        fprintf(stderr, "yacc: cannot create temporary files\n");
        exit(1);
    }
}

void clean_temps(void) {
    if (action_file) fclose(action_file);
    if (code_file) fclose(code_file);
    if (union_file) fclose(union_file);
}

void done(int k) {
    clean_temps();
    exit(k);
}

void no_space(void) {
    fprintf(stderr, "yacc: out of memory\n");
    done(2);
}

int main(int argc, char *argv[]) {
    get_args(argc, argv);
    open_files();
    init_symtab();
    
    reader();
    lr0();
    lalr();
    make_parser();
    lalr_free();
    
    create_output_file();
    verbose();
    output();
    
    if (vflag && verbose_file) {
        fclose(verbose_file);
    }
    if (dflag && defines_file) {
        fclose(defines_file);
    }
    if (output_file) {
        fclose(output_file);
    }

    done(0);
    return 0;
}
