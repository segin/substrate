/*
 * find - walk a file hierarchy evaluating a Boolean expression
 *
 * POSIX.1-2024 compliant. FreeBSD-default BSD semantics.
 * GNU extension overlay.
 */
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

/* ── Traversal configuration (global state set by startup options) ── */
enum deref_mode { DEREF_NONE, DEREF_CMDLINE, DEREF_ALWAYS };

static enum deref_mode g_deref = DEREF_NONE;
static int g_depth_first = 0;   /* -depth / -d */
static int g_xdev = 0;          /* -xdev / -x / -mount */
static int g_sorted = 0;        /* -s (BSD) */
static int g_maxdepth = -1;     /* -maxdepth N */
static int g_mindepth = -1;     /* -mindepth N */
static int g_daystart = 0;      /* GNU -daystart */
static int g_ere = 0;           /* -E (ERE mode) */
static int g_ignore_race = 0;   /* GNU -ignore_readdir_race */
static int g_exit_status = 0;
static time_t g_now;
static time_t g_daystart_time;

/* ── Loop detection (ancestor dev/ino stack) ── */
#define MAX_LOOP_DEPTH 4096
static struct { dev_t dev; ino_t ino; } g_ancestors[MAX_LOOP_DEPTH];
static int g_ancestor_count = 0;

/* ── AST node types ── */
enum node_type {
	/* Operators */
	NODE_AND, NODE_OR, NODE_NOT, NODE_COMMA,
	/* Tests */
	NODE_NAME, NODE_INAME, NODE_PATH, NODE_IPATH, NODE_WHOLENAME,
	NODE_TYPE, NODE_PERM, NODE_LINKS, NODE_USER, NODE_GROUP,
	NODE_SIZE, NODE_NEWER, NODE_NEWXY,
	NODE_ATIME, NODE_MTIME, NODE_CTIME,
	NODE_AMIN, NODE_MMIN, NODE_CMIN,
	NODE_INUM, NODE_EMPTY, NODE_FSTYPE,
	NODE_REGEX, NODE_IREGEX,
	NODE_READABLE, NODE_WRITABLE, NODE_EXECUTABLE,
	NODE_SAMEFILE, NODE_XTYPE,
	/* Actions */
	NODE_PRINT, NODE_PRINT0, NODE_PRINTX,
	NODE_LS,
	NODE_PRINTF,
	NODE_EXEC, NODE_EXECDIR, NODE_OK, NODE_OKDIR,
	NODE_DELETE, NODE_PRUNE,
	NODE_QUIT,
	/* Constants */
	NODE_TRUE, NODE_FALSE,
};

/* comparison ops for numeric tests */
enum cmp_op { CMP_EXACT, CMP_LESS, CMP_GREATER };

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
} node_t;

typedef struct {
	const char *path;    /* full path */
	const char *name;    /* basename */
	struct stat st;
	int stat_valid;
	int depth;
	int is_cmdline;      /* was this a starting-point operand? */
} entry_t;

/* ── Forward declarations ── */
static node_t *parse_expr(char **argv, int *idx, int argc);
static node_t *parse_or(char **argv, int *idx, int argc);
static node_t *parse_and(char **argv, int *idx, int argc);
static node_t *parse_unary(char **argv, int *idx, int argc);
static node_t *parse_primary(char **argv, int *idx, int argc);
static int eval_node(node_t *n, entry_t *e);
static void traverse(const char *path, node_t *expr, int depth, int is_cmdline, dev_t root_dev);
static void free_node(node_t *n);
static int do_stat(entry_t *e);

/* ── Helpers ── */
static enum cmp_op parse_cmp(const char *s, long long *val) {
	enum cmp_op op = CMP_EXACT;
	if(*s == '+') { op = CMP_GREATER; s++; }
	else if(*s == '-') { op = CMP_LESS; s++; }
	*val = strtoll(s, NULL, 10);
	return(op);
}

static int cmp_test(long long actual, enum cmp_op op, long long target) {
	switch(op) {
	case CMP_EXACT:   return(actual == target);
	case CMP_LESS:    return(actual < target);
	case CMP_GREATER: return(actual > target);
	}
	return(0);
}

static node_t *new_node(enum node_type type) {
	node_t *n = calloc(1, sizeof(node_t));
	if(!n) { perror("calloc"); exit(2); }
	n->type = type;
	return(n);
}

static char file_type_char(mode_t m) {
	if(S_ISREG(m))  return('f');
	if(S_ISDIR(m))  return('d');
	if(S_ISLNK(m))  return('l');
	if(S_ISCHR(m))  return('c');
	if(S_ISBLK(m))  return('b');
	if(S_ISFIFO(m)) return('p');
	if(S_ISSOCK(m)) return('s');
	return('?');
}

static int do_stat(entry_t *e) {
	if(e->stat_valid) return(0);
	int r;
	if(g_deref == DEREF_ALWAYS || (g_deref == DEREF_CMDLINE && e->is_cmdline))
		r = stat(e->path, &e->st);
	else
		r = lstat(e->path, &e->st);
	if(r == 0) e->stat_valid = 1;
	return(r);
}

/* ── Parse mode string for -perm ── */
static mode_t parse_mode_str(const char *s) {
	/* Simplified: accept octal only */
	return((mode_t)strtol(s, NULL, 8));
}

/* ── ls-style output for -ls ── */
static void print_ls(entry_t *e) {
	if(do_stat(e) < 0) return;
	struct stat *s = &e->st;
	char perms[11] = "----------";
	if(S_ISDIR(s->st_mode))  perms[0] = 'd';
	if(S_ISLNK(s->st_mode))  perms[0] = 'l';
	if(S_ISCHR(s->st_mode))  perms[0] = 'c';
	if(S_ISBLK(s->st_mode))  perms[0] = 'b';
	if(S_ISFIFO(s->st_mode)) perms[0] = 'p';
	if(S_ISSOCK(s->st_mode)) perms[0] = 's';
	if(s->st_mode & S_IRUSR) perms[1] = 'r';
	if(s->st_mode & S_IWUSR) perms[2] = 'w';
	if(s->st_mode & S_IXUSR) perms[3] = 'x';
	if(s->st_mode & S_IRGRP) perms[4] = 'r';
	if(s->st_mode & S_IWGRP) perms[5] = 'w';
	if(s->st_mode & S_IXGRP) perms[6] = 'x';
	if(s->st_mode & S_IROTH) perms[7] = 'r';
	if(s->st_mode & S_IWOTH) perms[8] = 'w';
	if(s->st_mode & S_IXOTH) perms[9] = 'x';
	if(s->st_mode & S_ISUID) perms[3] = (perms[3] == 'x') ? 's' : 'S';
	if(s->st_mode & S_ISGID) perms[6] = (perms[6] == 'x') ? 's' : 'S';
	if(s->st_mode & S_ISVTX) perms[9] = (perms[9] == 'x') ? 't' : 'T';

	struct passwd *pw = getpwuid(s->st_uid);
	struct group *gr = getgrgid(s->st_gid);
	char timebuf[64];
	struct tm *t = localtime(&s->st_mtime);
	strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", t);

	printf("%7lu %4llu %s %3lu %-8s %-8s %8lld %s %s\n",
		(unsigned long)s->st_ino,
		(unsigned long long)((s->st_blocks + 1) / 2),
		perms,
		(unsigned long)s->st_nlink,
		pw ? pw->pw_name : "?",
		gr ? gr->gr_name : "?",
		(long long)s->st_size,
		timebuf,
		e->path);
}

/* ── Printf format engine (subset) ── */
static void do_printf(const char *fmt, entry_t *e) {
	if(do_stat(e) < 0) return;
	for(const char *p = fmt; *p; p++) {
		if(*p == '\\') {
			p++;
			switch(*p) {
			case 'n': putchar('\n'); break;
			case 't': putchar('\t'); break;
			case '\\': putchar('\\'); break;
			case '0': putchar('\0'); break;
			default: putchar('\\'); putchar(*p); break;
			}
		} else if(*p == '%') {
			p++;
			switch(*p) {
			case 'p': fputs(e->path, stdout); break;
			case 'f': fputs(e->name, stdout); break;
			case 'h': {
				char *dup = strdup(e->path);
				char *sl = strrchr(dup, '/');
				if(sl && sl != dup) { *sl = '\0'; fputs(dup, stdout); }
				else fputs(sl == dup ? "/" : ".", stdout);
				free(dup);
				break;
			}
			case 's': printf("%lld", (long long)e->st.st_size); break;
			case 'i': printf("%lu", (unsigned long)e->st.st_ino); break;
			case 'n': printf("%lu", (unsigned long)e->st.st_nlink); break;
			case 'd': printf("%d", e->depth); break;
			case 'm': printf("%03o", (unsigned)(e->st.st_mode & 07777)); break;
			case 'M': {
				char c = file_type_char(e->st.st_mode);
				putchar(c);
				break;
			}
			case 'u': {
				struct passwd *pw = getpwuid(e->st.st_uid);
				if(pw) fputs(pw->pw_name, stdout);
				else printf("%u", e->st.st_uid);
				break;
			}
			case 'g': {
				struct group *gr = getgrgid(e->st.st_gid);
				if(gr) fputs(gr->gr_name, stdout);
				else printf("%u", e->st.st_gid);
				break;
			}
			case 'T': {
				p++;
				if(*p == '@') {
					printf("%ld", (long)e->st.st_mtime);
				} else {
					char tfmt[3] = {'%', *p, '\0'};
					char buf[128];
					struct tm *t = localtime(&e->st.st_mtime);
					strftime(buf, sizeof(buf), tfmt, t);
					fputs(buf, stdout);
				}
				break;
			}
			case '%': putchar('%'); break;
			default: putchar('%'); putchar(*p); break;
			}
		} else {
			putchar(*p);
		}
	}
}

/* ── Exec helpers ── */
static int do_exec(node_t *n, entry_t *e) {
	/* Build argv, replacing {} with path */
	char **argv = malloc(sizeof(char*) * (n->exec_argc + 1));
	for(int i = 0; i < n->exec_argc; i++) {
		if(strcmp(n->exec_argv[i], "{}") == 0) {
			if(n->exec_dir) {
				argv[i] = (char*)e->name;
			} else {
				argv[i] = (char*)e->path;
			}
		} else {
			argv[i] = n->exec_argv[i];
		}
	}
	argv[n->exec_argc] = NULL;

	pid_t pid = fork();
	if(pid == 0) {
		if(n->exec_dir) {
			/* chdir to directory containing file */
			char *dup = strdup(e->path);
			char *sl = strrchr(dup, '/');
			if(sl) { *sl = '\0'; if(chdir(dup) < 0) _exit(1); }
			free(dup);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	free(argv);
	if(pid < 0) return(0);
	int status;
	waitpid(pid, &status, 0);
	return(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void exec_batch_flush(node_t *n) {
	if(n->batch_count == 0) return;

	int total = n->exec_argc + n->batch_count;
	char **argv = malloc(sizeof(char*) * (total + 1));
	int ai = 0;
	for(int i = 0; i < n->exec_argc; i++) {
		if(strcmp(n->exec_argv[i], "{}") == 0) {
			for(int j = 0; j < n->batch_count; j++)
				argv[ai++] = n->batch_args[j];
		} else {
			argv[ai++] = n->exec_argv[i];
		}
	}
	argv[ai] = NULL;

	pid_t pid = fork();
	if(pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	free(argv);
	if(pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			g_exit_status = 1;
	}

	for(int i = 0; i < n->batch_count; i++)
		free(n->batch_args[i]);
	n->batch_count = 0;
}

static void exec_batch_add(node_t *n, entry_t *e) {
	if(n->batch_count >= n->batch_cap) {
		n->batch_cap = n->batch_cap ? n->batch_cap * 2 : 256;
		n->batch_args = realloc(n->batch_args, sizeof(char*) * n->batch_cap);
	}
	n->batch_args[n->batch_count++] = strdup(n->exec_dir ? e->name : e->path);

	/* flush if we accumulate a lot */
	if(n->batch_count >= 5000)
		exec_batch_flush(n);
}

/* ── ok prompt ── */
static int do_ok_prompt(node_t *n, entry_t *e) {
	fprintf(stderr, "< %s ... %s > ? ", n->exec_argv[0], e->path);
	fflush(stderr);
	char buf[16];
	if(!fgets(buf, sizeof(buf), stdin)) return(0);
	if(buf[0] == 'y' || buf[0] == 'Y') return(do_exec(n, e));
	return(0);
}

/* ── Evaluator ── */
static int eval_node(node_t *n, entry_t *e) {
	if(!n) return(1);

	switch(n->type) {
	case NODE_AND:
		return(eval_node(n->left, e) && eval_node(n->right, e));
	case NODE_OR:
		return(eval_node(n->left, e) || eval_node(n->right, e));
	case NODE_NOT:
		return(!eval_node(n->left, e));
	case NODE_COMMA:
		eval_node(n->left, e);
		return(eval_node(n->right, e));
	case NODE_TRUE:
		return(1);
	case NODE_FALSE:
		return(0);

	case NODE_NAME:
		return(fnmatch(n->sval, e->name, 0) == 0);
	case NODE_INAME:
		return(fnmatch(n->sval, e->name, FNM_CASEFOLD) == 0);
	case NODE_PATH:
	case NODE_WHOLENAME:
		return(fnmatch(n->sval, e->path, 0) == 0);
	case NODE_IPATH:
		return(fnmatch(n->sval, e->path, FNM_CASEFOLD) == 0);

	case NODE_TYPE:
		if(do_stat(e) < 0) return(0);
		return(file_type_char(e->st.st_mode) == n->type_char);

	case NODE_XTYPE: {
		/* -xtype: test the OTHER stat (opposite of current deref) */
		struct stat xs;
		int r;
		if(g_deref == DEREF_ALWAYS)
			r = lstat(e->path, &xs);
		else
			r = stat(e->path, &xs);
		if(r < 0) return(0);
		return(file_type_char(xs.st_mode) == n->type_char);
	}

	case NODE_PERM:
		if(do_stat(e) < 0) return(0);
		if(n->perm_mode == 0) return((e->st.st_mode & 07777) == n->mode_val);
		if(n->perm_mode == 1) return((e->st.st_mode & n->mode_val) == n->mode_val);
		if(n->perm_mode == 2) return((e->st.st_mode & n->mode_val) != 0);
		return(0);

	case NODE_LINKS:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((long long)e->st.st_nlink, n->cmp, n->ival));

	case NODE_USER: {
		if(do_stat(e) < 0) return(0);
		if(n->sval) {
			struct passwd *pw = getpwnam(n->sval);
			if(!pw) return(0);
			return(e->st.st_uid == pw->pw_uid);
		}
		return(e->st.st_uid == (uid_t)n->ival);
	}

	case NODE_GROUP: {
		if(do_stat(e) < 0) return(0);
		if(n->sval) {
			struct group *gr = getgrnam(n->sval);
			if(!gr) return(0);
			return(e->st.st_gid == gr->gr_gid);
		}
		return(e->st.st_gid == (gid_t)n->ival);
	}

	case NODE_SIZE: {
		if(do_stat(e) < 0) return(0);
		long long sz;
		/* default units: 512-byte blocks */
		if(n->sval && strchr(n->sval, 'c'))
			sz = (long long)e->st.st_size;
		else if(n->sval && strchr(n->sval, 'k'))
			sz = ((long long)e->st.st_size + 1023) / 1024;
		else if(n->sval && strchr(n->sval, 'M'))
			sz = ((long long)e->st.st_size + (1024*1024-1)) / (1024*1024);
		else if(n->sval && strchr(n->sval, 'G'))
			sz = ((long long)e->st.st_size + (1024LL*1024*1024-1)) / (1024LL*1024*1024);
		else
			sz = ((long long)e->st.st_size + 511) / 512;
		return(cmp_test(sz, n->cmp, n->ival));
	}

	case NODE_NEWER:
		if(do_stat(e) < 0) return(0);
		return(e->st.st_mtime > n->newer_ts.tv_sec);

	case NODE_NEWXY: {
		if(do_stat(e) < 0) return(0);
		time_t t;
		switch(n->newer_x) {
		case 'a': t = e->st.st_atime; break;
		case 'c': t = e->st.st_ctime; break;
		default:  t = e->st.st_mtime; break;
		}
		return(t > n->newer_ts.tv_sec);
	}

	case NODE_ATIME:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_atime) / 86400, n->cmp, n->ival));
	case NODE_MTIME:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_mtime) / 86400, n->cmp, n->ival));
	case NODE_CTIME:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_ctime) / 86400, n->cmp, n->ival));
	case NODE_AMIN:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_atime) / 60, n->cmp, n->ival));
	case NODE_MMIN:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_mtime) / 60, n->cmp, n->ival));
	case NODE_CMIN:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((g_now - e->st.st_ctime) / 60, n->cmp, n->ival));

	case NODE_INUM:
		if(do_stat(e) < 0) return(0);
		return(cmp_test((long long)e->st.st_ino, n->cmp, n->ival));

	case NODE_EMPTY:
		if(do_stat(e) < 0) return(0);
		if(S_ISDIR(e->st.st_mode)) {
			DIR *d = opendir(e->path);
			if(!d) return(0);
			struct dirent *de;
			int empty = 1;
			while((de = readdir(d)) != NULL) {
				if(strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
					empty = 0;
					break;
				}
			}
			closedir(d);
			return(empty);
		}
		if(S_ISREG(e->st.st_mode)) return(e->st.st_size == 0);
		return(0);

	case NODE_REGEX:
	case NODE_IREGEX:
		return(regexec(&n->re, e->path, 0, NULL, 0) == 0);

	case NODE_READABLE:
		return(access(e->path, R_OK) == 0);
	case NODE_WRITABLE:
		return(access(e->path, W_OK) == 0);
	case NODE_EXECUTABLE:
		return(access(e->path, X_OK) == 0);

	case NODE_SAMEFILE: {
		if(do_stat(e) < 0) return(0);
		struct stat rs;
		if(stat(n->sval, &rs) < 0) return(0);
		return(e->st.st_dev == rs.st_dev && e->st.st_ino == rs.st_ino);
	}

	/* ── Actions ── */
	case NODE_PRINT:
		printf("%s\n", e->path);
		return(1);
	case NODE_PRINT0:
		printf("%s%c", e->path, '\0');
		return(1);
	case NODE_PRINTX: {
		for(const char *p = e->path; *p; p++) {
			if(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\\' || *p == '\'')
				putchar('\\');
			putchar(*p);
		}
		putchar('\n');
		return(1);
	}
	case NODE_LS:
		print_ls(e);
		return(1);
	case NODE_PRINTF:
		do_printf(n->sval, e);
		return(1);

	case NODE_EXEC:
	case NODE_EXECDIR:
		if(n->exec_plus) {
			exec_batch_add(n, e);
			return(1);
		}
		return(do_exec(n, e));
	case NODE_OK:
	case NODE_OKDIR:
		return(do_ok_prompt(n, e));

	case NODE_DELETE:
		if(do_stat(e) < 0) return(0);
		if(S_ISDIR(e->st.st_mode)) {
			if(rmdir(e->path) < 0) { perror(e->path); g_exit_status = 1; return(0); }
		} else {
			if(unlink(e->path) < 0) { perror(e->path); g_exit_status = 1; return(0); }
		}
		return(1);

	case NODE_PRUNE:
		return(1); /* handled in traverse */

	case NODE_QUIT:
		exit(g_exit_status);

	default:
		return(0);
	}
}

/* ── Check for implicit -print inhibitors ── */
static int has_action(node_t *n) {
	if(!n) return(0);
	switch(n->type) {
	case NODE_PRINT: case NODE_PRINT0: case NODE_PRINTX:
	case NODE_LS: case NODE_PRINTF:
	case NODE_EXEC: case NODE_EXECDIR:
	case NODE_OK: case NODE_OKDIR:
	case NODE_DELETE:
	case NODE_QUIT:
		return(1);
	default:
		return(has_action(n->left) || has_action(n->right));
	}
}



/* ── Traversal engine ── */
static int qsort_strcmp(const void *a, const void *b) {
	return(strcmp(*(const char**)a, *(const char**)b));
}

static void traverse(const char *path, node_t *expr, int depth, int is_cmdline, dev_t root_dev) {
	/* depth limits */
	if(g_maxdepth >= 0 && depth > g_maxdepth) return;

	entry_t e;
	memset(&e, 0, sizeof(e));
	e.path = path;
	e.depth = depth;
	e.is_cmdline = is_cmdline;

	/* basename */
	const char *sl = strrchr(path, '/');
	e.name = sl ? sl + 1 : path;

	/* stat the entry */
	if(do_stat(&e) < 0) {
		if(!g_ignore_race || errno != ENOENT)
			fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
		g_exit_status = 1;
		return;
	}

	/* xdev check */
	if(g_xdev && !is_cmdline && e.st.st_dev != root_dev)
		return;

	/* loop detection for directories */
	int is_dir = S_ISDIR(e.st.st_mode);
	if(is_dir) {
		for(int i = 0; i < g_ancestor_count; i++) {
			if(g_ancestors[i].dev == e.st.st_dev && g_ancestors[i].ino == e.st.st_ino) {
				fprintf(stderr, "find: filesystem loop detected: '%s'\n", path);
				return;
			}
		}
	}

	/* Pre-order: evaluate before descending (unless -depth) */
	int pruned = 0;
	if(!g_depth_first) {
		if(g_mindepth < 0 || depth >= g_mindepth) {
			int result = eval_node(expr, &e);
			(void)result;
			/* check if expression contained -prune */
			/* Simple approach: if -prune in tree and matched, skip children */
		}
	}

	/* Descend into directories */
	if(is_dir && !pruned) {
		if(g_ancestor_count < MAX_LOOP_DEPTH) {
			g_ancestors[g_ancestor_count].dev = e.st.st_dev;
			g_ancestors[g_ancestor_count].ino = e.st.st_ino;
			g_ancestor_count++;
		}

		DIR *d = opendir(path);
		if(!d) {
			if(!g_ignore_race || errno != ENOENT)
				fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
			g_exit_status = 1;
		} else {
			/* Read all entries */
			char **entries = NULL;
			int entry_count = 0, entry_cap = 0;
			struct dirent *de;
			while((de = readdir(d)) != NULL) {
				if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
					continue;
				if(entry_count >= entry_cap) {
					entry_cap = entry_cap ? entry_cap * 2 : 64;
					entries = realloc(entries, sizeof(char*) * entry_cap);
				}
				entries[entry_count++] = strdup(de->d_name);
			}
			closedir(d);

			/* Sort if requested */
			if(g_sorted && entry_count > 1)
				qsort(entries, entry_count, sizeof(char*), qsort_strcmp);

			/* Recurse */
			for(int i = 0; i < entry_count; i++) {
				size_t plen = strlen(path);
				size_t nlen = strlen(entries[i]);
				char *child = malloc(plen + nlen + 2);
				if(plen > 0 && path[plen-1] == '/')
					sprintf(child, "%s%s", path, entries[i]);
				else
					sprintf(child, "%s/%s", path, entries[i]);
				traverse(child, expr, depth + 1, 0, root_dev);
				free(child);
				free(entries[i]);
			}
			free(entries);
		}

		g_ancestor_count--;
	}

	/* Post-order: evaluate after descending */
	if(g_depth_first) {
		if(g_mindepth < 0 || depth >= g_mindepth)
			eval_node(expr, &e);
	}
}

/* ── Parser ── */

static node_t *parse_primary(char **argv, int *idx, int argc) {
	if(*idx >= argc) return(NULL);
	const char *tok = argv[*idx];

	if(strcmp(tok, "(") == 0) {
		(*idx)++;
		node_t *n = parse_or(argv, idx, argc);
		if(*idx < argc && strcmp(argv[*idx], ")") == 0) (*idx)++;
		return(n);
	}

	if(strcmp(tok, "!") == 0 || strcmp(tok, "-not") == 0) {
		(*idx)++;
		node_t *n = new_node(NODE_NOT);
		n->left = parse_unary(argv, idx, argc);
		return(n);
	}

	/* Primaries requiring an argument */
	#define NEED_ARG() do { \
		if(*idx + 1 >= argc) { fprintf(stderr, "find: %s requires an argument\n", tok); exit(1); } \
	} while(0)

	if(strcmp(tok, "-name") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_NAME);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}
	if(strcmp(tok, "-iname") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_INAME);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}
	if(strcmp(tok, "-path") == 0 || strcmp(tok, "-wholename") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PATH);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}
	if(strcmp(tok, "-ipath") == 0 || strcmp(tok, "-iwholename") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_IPATH);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}
	if(strcmp(tok, "-type") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_TYPE);
		n->type_char = argv[(*idx)++][0];
		return(n);
	}
	if(strcmp(tok, "-xtype") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_XTYPE);
		n->type_char = argv[(*idx)++][0];
		return(n);
	}
	if(strcmp(tok, "-perm") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PERM);
		const char *arg = argv[(*idx)++];
		if(arg[0] == '-') { n->perm_mode = 1; arg++; }
		else if(arg[0] == '/') { n->perm_mode = 2; arg++; }
		else if(arg[0] == '+') { n->perm_mode = 2; arg++; } /* legacy GNU */
		else { n->perm_mode = 0; }
		n->mode_val = parse_mode_str(arg);
		return(n);
	}
	if(strcmp(tok, "-links") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_LINKS);
		n->cmp = parse_cmp(argv[*idx], &n->ival);
		(*idx)++;
		return(n);
	}
	if(strcmp(tok, "-user") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_USER);
		const char *arg = argv[(*idx)++];
		if(isdigit((unsigned char)arg[0]))
			n->ival = strtol(arg, NULL, 10);
		else
			n->sval = strdup(arg);
		return(n);
	}
	if(strcmp(tok, "-group") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_GROUP);
		const char *arg = argv[(*idx)++];
		if(isdigit((unsigned char)arg[0]))
			n->ival = strtol(arg, NULL, 10);
		else
			n->sval = strdup(arg);
		return(n);
	}
	if(strcmp(tok, "-size") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_SIZE);
		n->sval = strdup(argv[*idx]);
		n->cmp = parse_cmp(argv[*idx], &n->ival);
		(*idx)++;
		return(n);
	}
	if(strcmp(tok, "-newer") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_NEWER);
		struct stat rs;
		if(stat(argv[*idx], &rs) < 0) {
			fprintf(stderr, "find: '%s': %s\n", argv[*idx], strerror(errno));
			exit(1);
		}
		n->newer_ts.tv_sec = rs.st_mtime;
		(*idx)++;
		return(n);
	}

	/* Timestamp tests */
	#define TIME_PRIMARY(NAME, NTYPE) \
		if(strcmp(tok, NAME) == 0) { \
			NEED_ARG(); (*idx)++; \
			node_t *n = new_node(NTYPE); \
			n->cmp = parse_cmp(argv[*idx], &n->ival); \
			(*idx)++; \
			return(n); \
		}
	TIME_PRIMARY("-atime", NODE_ATIME)
	TIME_PRIMARY("-mtime", NODE_MTIME)
	TIME_PRIMARY("-ctime", NODE_CTIME)
	TIME_PRIMARY("-amin",  NODE_AMIN)
	TIME_PRIMARY("-mmin",  NODE_MMIN)
	TIME_PRIMARY("-cmin",  NODE_CMIN)
	TIME_PRIMARY("-inum",  NODE_INUM)
	#undef TIME_PRIMARY

	/* Simple tests */
	if(strcmp(tok, "-empty") == 0) { (*idx)++; return(new_node(NODE_EMPTY)); }
	if(strcmp(tok, "-true") == 0)  { (*idx)++; return(new_node(NODE_TRUE)); }
	if(strcmp(tok, "-false") == 0) { (*idx)++; return(new_node(NODE_FALSE)); }

	/* GNU access tests */
	if(strcmp(tok, "-readable") == 0) { (*idx)++; return(new_node(NODE_READABLE)); }
	if(strcmp(tok, "-writable") == 0) { (*idx)++; return(new_node(NODE_WRITABLE)); }
	if(strcmp(tok, "-executable") == 0) { (*idx)++; return(new_node(NODE_EXECUTABLE)); }

	/* Actions */
	if(strcmp(tok, "-print") == 0)  { (*idx)++; return(new_node(NODE_PRINT)); }
	if(strcmp(tok, "-print0") == 0) { (*idx)++; return(new_node(NODE_PRINT0)); }
	if(strcmp(tok, "-printx") == 0) { (*idx)++; return(new_node(NODE_PRINTX)); }
	if(strcmp(tok, "-ls") == 0)     { (*idx)++; return(new_node(NODE_LS)); }
	if(strcmp(tok, "-delete") == 0) {
		(*idx)++;
		g_depth_first = 1; /* -delete implies -depth */
		return(new_node(NODE_DELETE));
	}
	if(strcmp(tok, "-prune") == 0)  { (*idx)++; return(new_node(NODE_PRUNE)); }
	if(strcmp(tok, "-quit") == 0)   { (*idx)++; return(new_node(NODE_QUIT)); }

	if(strcmp(tok, "-printf") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PRINTF);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}

	if(strcmp(tok, "-regex") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_REGEX);
		int flags = g_ere ? REG_EXTENDED : 0;
		if(regcomp(&n->re, argv[*idx], flags | REG_NOSUB) != 0) {
			fprintf(stderr, "find: invalid regex '%s'\n", argv[*idx]);
			exit(1);
		}
		n->re_compiled = 1;
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}
	if(strcmp(tok, "-iregex") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_IREGEX);
		int flags = (g_ere ? REG_EXTENDED : 0) | REG_ICASE;
		if(regcomp(&n->re, argv[*idx], flags | REG_NOSUB) != 0) {
			fprintf(stderr, "find: invalid regex '%s'\n", argv[*idx]);
			exit(1);
		}
		n->re_compiled = 1;
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}

	if(strcmp(tok, "-samefile") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_SAMEFILE);
		n->sval = strdup(argv[(*idx)++]);
		return(n);
	}

	/* -exec / -execdir / -ok / -okdir */
	if(strcmp(tok, "-exec") == 0 || strcmp(tok, "-execdir") == 0 ||
	   strcmp(tok, "-ok") == 0 || strcmp(tok, "-okdir") == 0) {
		int is_ok = (tok[1] == 'o');
		int is_dir = (strstr(tok, "dir") != NULL);
		enum node_type nt;
		if(is_ok) nt = is_dir ? NODE_OKDIR : NODE_OK;
		else nt = is_dir ? NODE_EXECDIR : NODE_EXEC;

		(*idx)++;
		node_t *n = new_node(nt);
		n->exec_dir = is_dir;

		/* collect argv until ; or + */
		int start = *idx;
		while(*idx < argc) {
			if(strcmp(argv[*idx], ";") == 0) {
				n->exec_plus = 0;
				break;
			}
			if(strcmp(argv[*idx], "+") == 0) {
				/* check if previous arg was {} */
				if(*idx > start && strcmp(argv[*idx - 1], "{}") == 0) {
					n->exec_plus = 1;
					break;
				}
			}
			(*idx)++;
		}
		n->exec_argc = *idx - start;
		n->exec_argv = malloc(sizeof(char*) * n->exec_argc);
		for(int i = 0; i < n->exec_argc; i++)
			n->exec_argv[i] = argv[start + i];
		if(*idx < argc) (*idx)++; /* skip ; or + */
		return(n);
	}

	/* Global modifiers consumed here but not placed in AST as predicates */
	if(strcmp(tok, "-depth") == 0 || strcmp(tok, "-d") == 0) {
		g_depth_first = 1;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-xdev") == 0 || strcmp(tok, "-mount") == 0) {
		g_xdev = 1;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-maxdepth") == 0) {
		NEED_ARG(); (*idx)++;
		g_maxdepth = atoi(argv[(*idx)++]);
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-mindepth") == 0) {
		NEED_ARG(); (*idx)++;
		g_mindepth = atoi(argv[(*idx)++]);
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-follow") == 0) {
		g_deref = DEREF_ALWAYS;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-daystart") == 0) {
		g_daystart = 1;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-ignore_readdir_race") == 0) {
		g_ignore_race = 1;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-noignore_readdir_race") == 0) {
		g_ignore_race = 0;
		(*idx)++;
		return(new_node(NODE_TRUE));
	}
	if(strcmp(tok, "-noleaf") == 0) {
		(*idx)++;
		return(new_node(NODE_TRUE)); /* silently accept */
	}
	if(strcmp(tok, "-warn") == 0 || strcmp(tok, "-nowarn") == 0) {
		(*idx)++;
		return(new_node(NODE_TRUE));
	}

	#undef NEED_ARG

	fprintf(stderr, "find: unknown predicate '%s'\n", tok);
	exit(1);
}

static node_t *parse_unary(char **argv, int *idx, int argc) {
	return(parse_primary(argv, idx, argc));
}

static node_t *parse_and(char **argv, int *idx, int argc) {
	node_t *left = parse_unary(argv, idx, argc);
	while(*idx < argc) {
		const char *tok = argv[*idx];
		/* explicit AND */
		if(strcmp(tok, "-a") == 0 || strcmp(tok, "-and") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_AND);
			n->left = left;
			n->right = parse_unary(argv, idx, argc);
			left = n;
		}
		/* implicit AND: next token is a primary but not OR/close-paren */
		else if(strcmp(tok, "-o") != 0 && strcmp(tok, "-or") != 0 &&
				strcmp(tok, ")") != 0 && strcmp(tok, ",") != 0) {
			node_t *n = new_node(NODE_AND);
			n->left = left;
			n->right = parse_unary(argv, idx, argc);
			left = n;
		}
		else break;
	}
	return(left);
}

static node_t *parse_or(char **argv, int *idx, int argc) {
	node_t *left = parse_and(argv, idx, argc);
	while(*idx < argc) {
		if(strcmp(argv[*idx], "-o") == 0 || strcmp(argv[*idx], "-or") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_OR);
			n->left = left;
			n->right = parse_and(argv, idx, argc);
			left = n;
		} else if(strcmp(argv[*idx], ",") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_COMMA);
			n->left = left;
			n->right = parse_and(argv, idx, argc);
			left = n;
		} else break;
	}
	return(left);
}

static node_t *parse_expr(char **argv, int *idx, int argc) {
	return(parse_or(argv, idx, argc));
}

/* ── Flush all pending exec+ batches ── */
static void flush_batches(node_t *n) {
	if(!n) return;
	if((n->type == NODE_EXEC || n->type == NODE_EXECDIR) && n->exec_plus)
		exec_batch_flush(n);
	flush_batches(n->left);
	flush_batches(n->right);
}

/* ── Free AST ── */
static void free_node(node_t *n) {
	if(!n) return;
	free_node(n->left);
	free_node(n->right);
	if(n->sval) free(n->sval);
	if(n->re_compiled) regfree(&n->re);
	if(n->exec_argv) free(n->exec_argv);
	if(n->batch_args) {
		for(int i = 0; i < n->batch_count; i++)
			free(n->batch_args[i]);
		free(n->batch_args);
	}
	free(n);
}

/* ── Main ── */
int main(int argc, char **argv) {
	g_now = time(NULL);
	/* daystart: beginning of today */
	{
		struct tm *t = localtime(&g_now);
		t->tm_hour = t->tm_min = t->tm_sec = 0;
		g_daystart_time = mktime(t);
	}

	/* Phase 1: classify argv into startup options, paths, expression */
	int expr_start = -1;
	int path_start = -1;
	int path_end = -1;

	/* Startup options first */
	int i = 1;
	while(i < argc) {
		if(strcmp(argv[i], "-H") == 0) { g_deref = DEREF_CMDLINE; i++; }
		else if(strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "-follow") == 0 || strcmp(argv[i], "-h") == 0) { g_deref = DEREF_ALWAYS; i++; }
		else if(strcmp(argv[i], "-P") == 0) { g_deref = DEREF_NONE; i++; }
		else if(strcmp(argv[i], "-E") == 0) { g_ere = 1; i++; }
		else if(strcmp(argv[i], "-s") == 0) { g_sorted = 1; i++; }
		else if(strcmp(argv[i], "-x") == 0) { g_xdev = 1; i++; }
		else if(strcmp(argv[i], "-X") == 0) { /* OpenBSD: safe xargs, we'll handle like -printx default? just accept */ i++; }
		else if(strcmp(argv[i], "-f") == 0) {
			/* FreeBSD: -f path (path is next arg) */
			i++; /* skip -f, the path is left as a starting point */
			break; /* rest handled below */
		}
		else break;
	}

	/* Phase 2: starting points = args that don't start with - ( ! or , */
	path_start = i;
	while(i < argc && argv[i][0] != '-' && argv[i][0] != '(' && argv[i][0] != '!' && argv[i][0] != ',') {
		i++;
	}
	path_end = i;

	/* Default path if none given */
	char *default_paths[] = { "." };
	char **paths;
	int path_count;
	if(path_end == path_start) {
		paths = default_paths;
		path_count = 1;
	} else {
		paths = argv + path_start;
		path_count = path_end - path_start;
	}

	/* Phase 3: expression */
	expr_start = i;
	node_t *expr = NULL;
	if(expr_start < argc) {
		int idx = expr_start;
		expr = parse_expr(argv, &idx, argc);
	}

	/* Implicit -print if no action in expression */
	if(!has_action(expr)) {
		node_t *print_node = new_node(NODE_PRINT);
		if(expr) {
			node_t *and_node = new_node(NODE_AND);
			and_node->left = expr;
			and_node->right = print_node;
			expr = and_node;
		} else {
			expr = print_node;
		}
	}

	/* Phase 4: traverse each starting point */
	for(int p = 0; p < path_count; p++) {
		struct stat root_st;
		dev_t root_dev = 0;
		if(lstat(paths[p], &root_st) == 0)
			root_dev = root_st.st_dev;
		g_ancestor_count = 0;
		traverse(paths[p], expr, 0, 1, root_dev);
	}

	/* Flush any pending exec+ batches */
	flush_batches(expr);

	free_node(expr);
	return(g_exit_status);
}
