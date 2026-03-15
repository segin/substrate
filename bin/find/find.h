/*
 * find.h - shared declarations for find(1)
 *
 * Multi-dialect find implementation: POSIX.1-2024 core with
 * FreeBSD-default BSD semantics, OpenBSD/NetBSD deltas, and
 * GNU extension overlay.
 *
 * Architecture follows four semantic token classes:
 *   1. Startup options (-H, -L, -P, -E, -s, -X, -O, -D, -f)
 *   2. Global traversal modifiers (-depth, -xdev, -maxdepth, -follow, etc.)
 *   3. Pure tests (-name, -type, -perm, -newer, etc.)
 *   4. Side-effecting actions (-print, -exec, -delete, etc.)
 */
#ifndef FIND_H
#define FIND_H

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <regex.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>

#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif

/* ── Dereference policy (T04) ── */
enum deref_mode { DEREF_NONE, DEREF_CMDLINE, DEREF_ALWAYS };

/* ── AST node types (T05) ── */
enum node_type {
	/* Operators */
	NODE_AND, NODE_OR, NODE_NOT, NODE_COMMA,
	/* Tests (pure — no side effects) */
	NODE_NAME, NODE_INAME, NODE_PATH, NODE_IPATH, NODE_WHOLENAME,
	NODE_TYPE, NODE_PERM, NODE_LINKS, NODE_USER, NODE_GROUP,
	NODE_SIZE, NODE_NEWER, NODE_NEWXY,
	NODE_ATIME, NODE_MTIME, NODE_CTIME,
	NODE_AMIN, NODE_MMIN, NODE_CMIN,
	NODE_INUM, NODE_EMPTY, NODE_FSTYPE,
	NODE_REGEX, NODE_IREGEX,
	NODE_READABLE, NODE_WRITABLE, NODE_EXECUTABLE,
	NODE_SAMEFILE, NODE_XTYPE, NODE_ILNAME,
	/* Actions (side-effecting) */
	NODE_PRINT, NODE_PRINT0, NODE_PRINTX,
	NODE_LS,
	NODE_PRINTF,
	NODE_EXEC, NODE_EXECDIR, NODE_OK, NODE_OKDIR,
	NODE_DELETE, NODE_PRUNE,
	NODE_QUIT,
	NODE_FPRINT, NODE_FPRINT0, NODE_FLS, NODE_FPRINTF,
	/* Constants */
	NODE_TRUE, NODE_FALSE,
};

/* Comparison ops for numeric tests */
enum cmp_op { CMP_EXACT, CMP_LESS, CMP_GREATER };

/* ── AST node ── */
typedef struct node {
	enum node_type type;
	struct node *left;
	struct node *right;
	/* Primary data */
	char *sval;          /* pattern, username, format string, etc */
	long long ival;      /* numeric value */
	enum cmp_op cmp;     /* for numeric comparisons */
	mode_t mode_val;     /* for -perm */
	int perm_mode;       /* 0=exact, 1=all(-), 2=any(/) */
	char type_char;      /* for -type */
	regex_t re;          /* compiled regex */
	int re_compiled;
	/* exec data */
	char **exec_argv;
	int exec_argc;
	int exec_plus;       /* 1 for {} + */
	int exec_dir;        /* 1 for -execdir */
	/* exec+ batching */
	char **batch_args;
	int batch_count;
	int batch_cap;
	/* newerXY */
	char newer_x, newer_y;
	struct timespec newer_ts;
	/* file-output */
	FILE *out_fp;
} node_t;

/* ── Per-entry state passed to evaluator ── */
typedef struct {
	const char *path;    /* full path */
	const char *name;    /* basename */
	struct stat st;
	int stat_valid;
	int depth;
	int is_cmdline;      /* was this a starting-point operand? */
} entry_t;

/* ── Debug flags (GNU -D) ── */
enum debug_flag {
	DEBUG_NONE  = 0,
	DEBUG_TREE  = 1 << 0,   /* show parsed expression tree */
	DEBUG_STAT  = 1 << 1,   /* trace stat calls */
	DEBUG_OPT   = 1 << 2,   /* show optimizer actions */
	DEBUG_RATES = 1 << 3,   /* predicate success rates */
	DEBUG_EXEC  = 1 << 4,   /* trace exec calls */
	DEBUG_HELP  = 1 << 5,   /* print debug help and exit */
	DEBUG_ALL   = 0x3F,
};

/* ── Global traversal state ── */
extern enum deref_mode g_deref;
extern int g_depth_first;
extern int g_xdev;
extern int g_sorted;
extern int g_maxdepth;
extern int g_mindepth;
extern int g_daystart;
extern int g_ere;
extern int g_ignore_race;
extern int g_exit_status;
extern int g_pruned;
extern int g_opt_level;
extern unsigned g_debug;
extern time_t g_now;
extern time_t g_daystart_time;

/* ── Helpers (find_eval.c) ── */
node_t *new_node(enum node_type type);
enum cmp_op parse_cmp(const char *s, long long *val);
int cmp_test(long long actual, enum cmp_op op, long long target);
mode_t parse_mode_str(const char *s);
char file_type_char(mode_t m);
int do_stat(entry_t *e);

/* ── Evaluator (find_eval.c) ── */
int eval_node(node_t *n, entry_t *e);
int has_action(node_t *n);
void flush_batches(node_t *n);
void free_node(node_t *n);

/* ── Parser (find_parse.c) ── */
node_t *parse_expr(char **argv, int *idx, int argc);
node_t *optimize_ast(node_t *n, int opt_level);

/* ── Traversal (find_traverse.c) ── */
void traverse(const char *path, node_t *expr, int depth,
              int is_cmdline, dev_t root_dev);

#endif /* FIND_H */
