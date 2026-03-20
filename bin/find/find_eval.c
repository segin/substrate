/*
 * find_eval.c - expression evaluator, output actions, exec helpers
 */
#include "find.h"

/* ── Node constructor ── */
node_t *new_node(enum node_type type)
{
	node_t *n = calloc(1, sizeof(node_t));
	if (!n) { perror("calloc"); exit(2); }
	n->type = type;
	return n;
}

/* ── Comparison helpers ── */
enum cmp_op parse_cmp(const char *s, long long *val)
{
	enum cmp_op op = CMP_EXACT;
	if (*s == '+') { op = CMP_GREATER; s++; }
	else if (*s == '-') { op = CMP_LESS; s++; }
	*val = strtoll(s, NULL, 10);
	return op;
}

int cmp_test(long long actual, enum cmp_op op, long long target)
{
	switch (op) {
	case CMP_EXACT:   return actual == target;
	case CMP_LESS:    return actual < target;
	case CMP_GREATER: return actual > target;
	}
	return 0;
}

/* ── Stat helper ── */
int do_stat(entry_t *e)
{
	if (e->stat_valid) return 0;
	int r;
	if (g_deref == DEREF_ALWAYS ||
	    (g_deref == DEREF_CMDLINE && e->is_cmdline))
		r = stat(e->path, &e->st);
	else
		r = lstat(e->path, &e->st);
	if (g_debug & DEBUG_STAT)
		fprintf(stderr, "find: %s('%s') = %d\n",
		        (g_deref == DEREF_ALWAYS ||
		         (g_deref == DEREF_CMDLINE && e->is_cmdline))
		        ? "stat" : "lstat",
		        e->path, r);
	if (r == 0) e->stat_valid = 1;
	return r;
}

/* ── Mode string parser ── */
mode_t parse_mode_str(const char *s)
{
	return (mode_t)strtol(s, NULL, 8);
}

/* ── File type character ── */
char file_type_char(mode_t m)
{
	if (S_ISREG(m))  return 'f';
	if (S_ISDIR(m))  return 'd';
	if (S_ISLNK(m))  return 'l';
	if (S_ISCHR(m))  return 'c';
	if (S_ISBLK(m))  return 'b';
	if (S_ISFIFO(m)) return 'p';
	if (S_ISSOCK(m)) return 's';
	return '?';
}

/* ── ls-style output ── */
static void print_ls(entry_t *e)
{
	if (do_stat(e) < 0) return;
	struct stat *s = &e->st;
	char perms[11] = "----------";
	if (S_ISDIR(s->st_mode))  perms[0] = 'd';
	if (S_ISLNK(s->st_mode))  perms[0] = 'l';
	if (S_ISCHR(s->st_mode))  perms[0] = 'c';
	if (S_ISBLK(s->st_mode))  perms[0] = 'b';
	if (S_ISFIFO(s->st_mode)) perms[0] = 'p';
	if (S_ISSOCK(s->st_mode)) perms[0] = 's';
	if (s->st_mode & S_IRUSR) perms[1] = 'r';
	if (s->st_mode & S_IWUSR) perms[2] = 'w';
	if (s->st_mode & S_IXUSR) perms[3] = 'x';
	if (s->st_mode & S_IRGRP) perms[4] = 'r';
	if (s->st_mode & S_IWGRP) perms[5] = 'w';
	if (s->st_mode & S_IXGRP) perms[6] = 'x';
	if (s->st_mode & S_IROTH) perms[7] = 'r';
	if (s->st_mode & S_IWOTH) perms[8] = 'w';
	if (s->st_mode & S_IXOTH) perms[9] = 'x';
	if (s->st_mode & S_ISUID) perms[3] = (perms[3] == 'x') ? 's' : 'S';
	if (s->st_mode & S_ISGID) perms[6] = (perms[6] == 'x') ? 's' : 'S';
	if (s->st_mode & S_ISVTX) perms[9] = (perms[9] == 'x') ? 't' : 'T';

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

/* ── ls-style output to file ── */
static void fprint_ls(FILE *fp, entry_t *e)
{
	if (do_stat(e) < 0) return;
	struct stat *s = &e->st;
	char perms[11] = "----------";
	if (S_ISDIR(s->st_mode))  perms[0] = 'd';
	if (S_ISLNK(s->st_mode))  perms[0] = 'l';
	if (S_ISCHR(s->st_mode))  perms[0] = 'c';
	if (S_ISBLK(s->st_mode))  perms[0] = 'b';
	if (S_ISFIFO(s->st_mode)) perms[0] = 'p';
	if (S_ISSOCK(s->st_mode)) perms[0] = 's';
	if (s->st_mode & S_IRUSR) perms[1] = 'r';
	if (s->st_mode & S_IWUSR) perms[2] = 'w';
	if (s->st_mode & S_IXUSR) perms[3] = 'x';
	if (s->st_mode & S_IRGRP) perms[4] = 'r';
	if (s->st_mode & S_IWGRP) perms[5] = 'w';
	if (s->st_mode & S_IXGRP) perms[6] = 'x';
	if (s->st_mode & S_IROTH) perms[7] = 'r';
	if (s->st_mode & S_IWOTH) perms[8] = 'w';
	if (s->st_mode & S_IXOTH) perms[9] = 'x';
	if (s->st_mode & S_ISUID) perms[3] = (perms[3] == 'x') ? 's' : 'S';
	if (s->st_mode & S_ISGID) perms[6] = (perms[6] == 'x') ? 's' : 'S';
	if (s->st_mode & S_ISVTX) perms[9] = (perms[9] == 'x') ? 't' : 'T';

	struct passwd *pw = getpwuid(s->st_uid);
	struct group *gr = getgrgid(s->st_gid);
	char timebuf[64];
	struct tm *t = localtime(&s->st_mtime);
	strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", t);

	fprintf(fp, "%7lu %4llu %s %3lu %-8s %-8s %8lld %s %s\n",
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

/* ── Printf format engine ── */
static void do_printf(FILE *fp, const char *fmt, entry_t *e)
{
	if (do_stat(e) < 0) return;
	for (const char *p = fmt; *p; p++) {
		if (*p == '\\') {
			p++;
			switch (*p) {
			case 'n': fputc('\n', fp); break;
			case 't': fputc('\t', fp); break;
			case '\\': fputc('\\', fp); break;
			case '0': fputc('\0', fp); break;
			default: fputc('\\', fp); fputc(*p, fp); break;
			}
		} else if (*p == '%') {
			p++;
			switch (*p) {
			case 'p': fputs(e->path, fp); break;
			case 'f': fputs(e->name, fp); break;
			case 'h': {
				char *dup = strdup(e->path);
				char *sl = strrchr(dup, '/');
				if (sl && sl != dup) {
					*sl = '\0'; fputs(dup, fp);
				} else {
					fputs(sl == dup ? "/" : ".", fp);
				}
				free(dup);
				break;
			}
			case 's': fprintf(fp, "%lld", (long long)e->st.st_size); break;
			case 'i': fprintf(fp, "%lu", (unsigned long)e->st.st_ino); break;
			case 'n': fprintf(fp, "%lu", (unsigned long)e->st.st_nlink); break;
			case 'd': fprintf(fp, "%d", e->depth); break;
			case 'm': fprintf(fp, "%03o", (unsigned)(e->st.st_mode & 07777)); break;
			case 'M': {
				/* Full strmode-style permission string */
				char perm[11] = "----------";
				mode_t m = e->st.st_mode;
				if (S_ISDIR(m))  perm[0] = 'd';
				else if (S_ISLNK(m))  perm[0] = 'l';
				else if (S_ISCHR(m))  perm[0] = 'c';
				else if (S_ISBLK(m))  perm[0] = 'b';
				else if (S_ISFIFO(m)) perm[0] = 'p';
				else if (S_ISSOCK(m)) perm[0] = 's';
				if (m & S_IRUSR) perm[1] = 'r';
				if (m & S_IWUSR) perm[2] = 'w';
				if (m & S_IXUSR) perm[3] = 'x';
				if (m & S_IRGRP) perm[4] = 'r';
				if (m & S_IWGRP) perm[5] = 'w';
				if (m & S_IXGRP) perm[6] = 'x';
				if (m & S_IROTH) perm[7] = 'r';
				if (m & S_IWOTH) perm[8] = 'w';
				if (m & S_IXOTH) perm[9] = 'x';
				if (m & S_ISUID) perm[3] = (perm[3] == 'x') ? 's' : 'S';
				if (m & S_ISGID) perm[6] = (perm[6] == 'x') ? 's' : 'S';
				if (m & S_ISVTX) perm[9] = (perm[9] == 'x') ? 't' : 'T';
				fputs(perm, fp);
				break;
			}
			case 'u': {
				struct passwd *pw = getpwuid(e->st.st_uid);
				if (pw) fputs(pw->pw_name, fp);
				else fprintf(fp, "%u", e->st.st_uid);
				break;
			}
			case 'g': {
				struct group *gr = getgrgid(e->st.st_gid);
				if (gr) fputs(gr->gr_name, fp);
				else fprintf(fp, "%u", e->st.st_gid);
				break;
			}
			case 'T': {
				p++;
				if (*p == '@') {
					fprintf(fp, "%ld", (long)e->st.st_mtime);
				} else {
					char tfmt[3] = {'%', *p, '\0'};
					char buf[128];
					struct tm *t = localtime(&e->st.st_mtime);
					strftime(buf, sizeof(buf), tfmt, t);
					fputs(buf, fp);
				}
				break;
			}
			case 'A': {
				p++;
				if (*p == '@') {
					fprintf(fp, "%ld", (long)e->st.st_atime);
				} else {
					char tfmt[3] = {'%', *p, '\0'};
					char buf[128];
					struct tm *t = localtime(&e->st.st_atime);
					strftime(buf, sizeof(buf), tfmt, t);
					fputs(buf, fp);
				}
				break;
			}
			case 'C': {
				p++;
				if (*p == '@') {
					fprintf(fp, "%ld", (long)e->st.st_ctime);
				} else {
					char tfmt[3] = {'%', *p, '\0'};
					char buf[128];
					struct tm *t = localtime(&e->st.st_ctime);
					strftime(buf, sizeof(buf), tfmt, t);
					fputs(buf, fp);
				}
				break;
			}
			case 'k':
				fprintf(fp, "%lld", ((long long)e->st.st_size + 1023) / 1024);
				break;
			case 'b':
				fprintf(fp, "%lld", (long long)e->st.st_blocks);
				break;
			case 'l': {
				/* symlink target */
				char target[PATH_MAX];
				ssize_t len = readlink(e->path, target, sizeof(target) - 1);
				if (len >= 0) { target[len] = '\0'; fputs(target, fp); }
				break;
			}
			case 'y': fputc(file_type_char(e->st.st_mode), fp); break;
			case 'D': fprintf(fp, "%lu", (unsigned long)e->st.st_dev); break;
			case 'G': fprintf(fp, "%u", e->st.st_gid); break;
			case 'U': fprintf(fp, "%u", e->st.st_uid); break;
			case '%': fputc('%', fp); break;
			default: fputc('%', fp); fputc(*p, fp); break;
			}
		} else {
			fputc(*p, fp);
		}
	}
}

/* ── Exec helpers ── */
static int do_exec(node_t *n, entry_t *e)
{
	char **argv = malloc(sizeof(char *) * (n->exec_argc + 1));
	for (int i = 0; i < n->exec_argc; i++) {
		if (strcmp(n->exec_argv[i], "{}") == 0)
			argv[i] = (char *)(n->exec_dir ? e->name : e->path);
		else
			argv[i] = n->exec_argv[i];
	}
	argv[n->exec_argc] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		if (n->exec_dir) {
			char *dup = strdup(e->path);
			char *sl = strrchr(dup, '/');
			if (sl) { *sl = '\0'; if (chdir(dup) < 0) _exit(1); }
			free(dup);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	free(argv);
	if (pid < 0) return 0;
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void exec_batch_flush(node_t *n)
{
	if (n->batch_count == 0) return;

	int total = n->exec_argc + n->batch_count;
	char **argv = malloc(sizeof(char *) * (total + 1));
	int ai = 0;
	for (int i = 0; i < n->exec_argc; i++) {
		if (strcmp(n->exec_argv[i], "{}") == 0) {
			for (int j = 0; j < n->batch_count; j++)
				argv[ai++] = n->batch_args[j];
		} else {
			argv[ai++] = n->exec_argv[i];
		}
	}
	argv[ai] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		if (n->exec_dir) {
			/* For -execdir +, chdir to the common directory is not possible
			 * since batch may span directories. Just run in cwd. */
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	free(argv);
	if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			g_exit_status = 1;
	}

	for (int i = 0; i < n->batch_count; i++)
		free(n->batch_args[i]);
	n->batch_count = 0;
}

static void exec_batch_add(node_t *n, entry_t *e)
{
	if (n->batch_count >= n->batch_cap) {
		n->batch_cap = n->batch_cap ? n->batch_cap * 2 : 256;
		n->batch_args = realloc(n->batch_args, sizeof(char *) * n->batch_cap);
	}
	n->batch_args[n->batch_count++] = strdup(n->exec_dir ? e->name : e->path);

	/* Flush at ARG_MAX pressure or BSD 5000-pathname cap */
	long arg_max = sysconf(_SC_ARG_MAX);
	if (arg_max <= 0) arg_max = 131072;
	/* Estimate current arg size */
	size_t argsz = 0;
	for (int i = 0; i < n->batch_count; i++)
		argsz += strlen(n->batch_args[i]) + 1;
	if (n->batch_count >= 5000 || (long)argsz >= arg_max / 2)
		exec_batch_flush(n);
}

/* ── ok prompt ── */
static int do_ok_prompt(node_t *n, entry_t *e)
{
	fprintf(stderr, "< %s ... %s > ? ", n->exec_argv[0], e->path);
	fflush(stderr);
	char buf[16];
	if (!fgets(buf, sizeof(buf), stdin)) return 0;
	if (buf[0] == 'y' || buf[0] == 'Y') return do_exec(n, e);
	return 0;
}

/* ── PATH safety check for -execdir (T11: REQ-FIND-102) ── */
static int check_execdir_path_safety(void)
{
	char *path_env = getenv("PATH");
	if (!path_env) return 1;

	const char *p = path_env;
	while (1) {
		const char *end = strchr(p, ':');
		size_t len = end ? (size_t)(end - p) : strlen(p);

		if (len == 0 || (len == 1 && p[0] == '.') || (len >= 2 && p[0] == '.' && p[1] == '/')) {
			fprintf(stderr, "find: The current directory is included "
			        "in the PATH environment variable, which is "
			        "insecure in combination with the -execdir action.\n");
			return 0;
		} else if (len > 0 && p[0] != '/') {
			fprintf(stderr, "find: A relative path is included "
			        "in the PATH environment variable, which is "
			        "insecure in combination with the -execdir action.\n");
			return 0;
		}

		if (!end) break;
		p = end + 1;
	}
	return 1;
}

/* ── Evaluator ── */
int eval_node(node_t *n, entry_t *e)
{
	if (!n) return 1;

	switch (n->type) {
	/* ── Operators ── */
	case NODE_AND:
		return eval_node(n->left, e) && eval_node(n->right, e);
	case NODE_OR:
		return eval_node(n->left, e) || eval_node(n->right, e);
	case NODE_NOT:
		return !eval_node(n->left, e);
	case NODE_COMMA:
		eval_node(n->left, e);
		return eval_node(n->right, e);
	case NODE_TRUE:
		return 1;
	case NODE_FALSE:
		return 0;

	/* ── Tests (pure — no side effects) ── */
	case NODE_NAME:
		return fnmatch(n->sval, e->name, 0) == 0;
	case NODE_INAME:
		return fnmatch(n->sval, e->name, FNM_CASEFOLD) == 0;
	case NODE_PATH:
	case NODE_WHOLENAME:
		return fnmatch(n->sval, e->path, 0) == 0;
	case NODE_IPATH:
		return fnmatch(n->sval, e->path, FNM_CASEFOLD) == 0;

	case NODE_TYPE:
		if (do_stat(e) < 0) return 0;
		return file_type_char(e->st.st_mode) == n->type_char;

	case NODE_XTYPE: {
		struct stat xs;
		int r;
		if (g_deref == DEREF_ALWAYS)
			r = lstat(e->path, &xs);
		else
			r = stat(e->path, &xs);
		if (r < 0) return 0;
		return file_type_char(xs.st_mode) == n->type_char;
	}

	case NODE_PERM:
		if (do_stat(e) < 0) return 0;
		if (n->perm_mode == 0) return (e->st.st_mode & 07777) == n->mode_val;
		if (n->perm_mode == 1) return (e->st.st_mode & n->mode_val) == n->mode_val;
		if (n->perm_mode == 2) return (e->st.st_mode & n->mode_val) != 0;
		return 0;

	case NODE_LINKS:
		if (do_stat(e) < 0) return 0;
		return cmp_test((long long)e->st.st_nlink, n->cmp, n->ival);

	case NODE_USER: {
		if (do_stat(e) < 0) return 0;
		if (n->sval) {
			struct passwd *pw = getpwnam(n->sval);
			if (!pw) return 0;
			return e->st.st_uid == pw->pw_uid;
		}
		return e->st.st_uid == (uid_t)n->ival;
	}

	case NODE_GROUP: {
		if (do_stat(e) < 0) return 0;
		if (n->sval) {
			struct group *gr = getgrnam(n->sval);
			if (!gr) return 0;
			return e->st.st_gid == gr->gr_gid;
		}
		return e->st.st_gid == (gid_t)n->ival;
	}

	case NODE_SIZE: {
		if (do_stat(e) < 0) return 0;
		long long sz;
		if (n->sval && strchr(n->sval, 'c'))
			sz = (long long)e->st.st_size;
		else if (n->sval && strchr(n->sval, 'k'))
			sz = ((long long)e->st.st_size + 1023) / 1024;
		else if (n->sval && strchr(n->sval, 'M'))
			sz = ((long long)e->st.st_size + (1024 * 1024 - 1)) / (1024 * 1024);
		else if (n->sval && strchr(n->sval, 'G'))
			sz = ((long long)e->st.st_size + (1024LL * 1024 * 1024 - 1)) / (1024LL * 1024 * 1024);
		else
			sz = ((long long)e->st.st_size + 511) / 512;
		return cmp_test(sz, n->cmp, n->ival);
	}

	case NODE_NEWER:
		if (do_stat(e) < 0) return 0;
		return e->st.st_mtime > n->newer_ts.tv_sec;

	case NODE_NEWXY: {
		if (do_stat(e) < 0) return 0;
		time_t t;
		switch (n->newer_x) {
		case 'a': t = e->st.st_atime; break;
		case 'c': t = e->st.st_ctime; break;
		default:  t = e->st.st_mtime; break;
		}
		return t > n->newer_ts.tv_sec;
	}

	case NODE_ATIME:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_atime) / 86400, n->cmp, n->ival);
	case NODE_MTIME:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_mtime) / 86400, n->cmp, n->ival);
	case NODE_CTIME:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_ctime) / 86400, n->cmp, n->ival);
	case NODE_AMIN:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_atime) / 60, n->cmp, n->ival);
	case NODE_MMIN:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_mtime) / 60, n->cmp, n->ival);
	case NODE_CMIN:
		if (do_stat(e) < 0) return 0;
		return cmp_test((g_now - e->st.st_ctime) / 60, n->cmp, n->ival);

	case NODE_INUM:
		if (do_stat(e) < 0) return 0;
		return cmp_test((long long)e->st.st_ino, n->cmp, n->ival);

	case NODE_EMPTY:
		if (do_stat(e) < 0) return 0;
		if (S_ISDIR(e->st.st_mode)) {
			DIR *d = opendir(e->path);
			if (!d) return 0;
			struct dirent *de;
			int empty = 1;
			while ((de = readdir(d)) != NULL) {
				if (strcmp(de->d_name, ".") != 0 &&
				    strcmp(de->d_name, "..") != 0) {
					empty = 0;
					break;
				}
			}
			closedir(d);
			return empty;
		}
		if (S_ISREG(e->st.st_mode)) return e->st.st_size == 0;
		return 0;

	case NODE_FSTYPE: {
		if (do_stat(e) < 0) return 0;
		struct statvfs vfs;
		if (statvfs(e->path, &vfs) < 0) return 0;
		/* Compare filesystem type name from f_basetype/f_fstypename */
#if defined(__linux__)
		/* Linux statvfs doesn't have f_basetype; use /proc/mounts or
		 * statfs(2) f_type. For now, use a basic heuristic. */
		(void)vfs;
		struct statfs sfs;
		if (statfs(e->path, &sfs) < 0) return 0;
		/* Map common f_type values to names */
		const char *fsname = "unknown";
		switch (sfs.f_type) {
		case 0xEF53:     fsname = "ext2"; break;   /* ext2/3/4 */
		case 0x9123683E: fsname = "btrfs"; break;
		case 0x58465342: fsname = "xfs"; break;
		case 0x01021994: fsname = "tmpfs"; break;
		case 0x9FA0:     fsname = "proc"; break;
		case 0x62656572: fsname = "sysfs"; break;
		case 0x64626720: fsname = "debugfs"; break;
		case 0x4D44:     fsname = "msdos"; break;   /* FAT/VFAT */
		case 0x4006:     fsname = "fat"; break;
		case 0x5346544E: fsname = "ntfs"; break;
		case 0x6969:     fsname = "nfs"; break;
		case 0xFF534D42: fsname = "cifs"; break;
		case 0x52654973: fsname = "reiserfs"; break;
		case 0x3153464A: fsname = "jfs"; break;
		case 0xF15F:     fsname = "ecryptfs"; break;
		case 0x794C7630: fsname = "overlayfs"; break;
		case 0x2FC12FC1: fsname = "zfs"; break;
		case 0x0BD00BD0: fsname = "lustre"; break;
		}
		return strcasecmp(n->sval, fsname) == 0;
#else
		/* BSD: statvfs has f_fstypename */
		return strcasecmp(n->sval, vfs.f_fstypename) == 0;
#endif
	}

	case NODE_REGEX:
	case NODE_IREGEX:
		return regexec(&n->re, e->path, 0, NULL, 0) == 0;

	case NODE_READABLE:
		return access(e->path, R_OK) == 0;
	case NODE_WRITABLE:
		return access(e->path, W_OK) == 0;
	case NODE_EXECUTABLE:
		return access(e->path, X_OK) == 0;

	case NODE_SAMEFILE: {
		if (do_stat(e) < 0) return 0;
		struct stat rs;
		int r = (g_deref == DEREF_ALWAYS) ? stat(n->sval, &rs)
		                                   : lstat(n->sval, &rs);
		if (r < 0) return 0;
		return e->st.st_dev == rs.st_dev && e->st.st_ino == rs.st_ino;
	}

	case NODE_ILNAME: {
		char target[PATH_MAX];
		ssize_t len = readlink(e->path, target, sizeof(target) - 1);
		if (len < 0) return 0;
		target[len] = '\0';
		return fnmatch(n->sval, target, FNM_CASEFOLD) == 0;
	}

	/* ── Actions (side-effecting) ── */
	case NODE_PRINT:
		printf("%s\n", e->path);
		return 1;
	case NODE_PRINT0:
		printf("%s%c", e->path, '\0');
		return 1;
	case NODE_PRINTX: {
		for (const char *p = e->path; *p; p++) {
			if (*p == ' ' || *p == '\t' || *p == '\n' ||
			    *p == '\\' || *p == '\'')
				putchar('\\');
			putchar(*p);
		}
		putchar('\n');
		return 1;
	}
	case NODE_LS:
		print_ls(e);
		return 1;
	case NODE_PRINTF:
		do_printf(stdout, n->sval, e);
		return 1;

	case NODE_FPRINT:
		if (n->out_fp) fprintf(n->out_fp, "%s\n", e->path);
		return 1;
	case NODE_FPRINT0:
		if (n->out_fp) { fputs(e->path, n->out_fp); fputc('\0', n->out_fp); }
		return 1;
	case NODE_FLS:
		if (n->out_fp) fprint_ls(n->out_fp, e);
		return 1;
	case NODE_FPRINTF:
		if (n->out_fp) do_printf(n->out_fp, n->sval, e);
		return 1;

	case NODE_EXEC:
	case NODE_EXECDIR:
		if (n->exec_dir && !check_execdir_path_safety()) {
			g_exit_status = 1;
			return 0;
		}
		if (n->exec_plus) {
			exec_batch_add(n, e);
			return 1;
		}
		return do_exec(n, e);
	case NODE_OK:
	case NODE_OKDIR:
		return do_ok_prompt(n, e);

	case NODE_DELETE:
		if (do_stat(e) < 0) return 0;
		if (S_ISDIR(e->st.st_mode)) {
			if (rmdir(e->path) < 0) {
				perror(e->path);
				g_exit_status = 1;
				return 0;
			}
		} else {
			if (unlink(e->path) < 0) {
				perror(e->path);
				g_exit_status = 1;
				return 0;
			}
		}
		return 1;

	case NODE_PRUNE:
		g_pruned = 1;
		return 1;

	case NODE_QUIT:
		exit(g_exit_status);

	default:
		return 0;
	}
}

/* ── Check for implicit -print inhibitors ── */
int has_action(node_t *n)
{
	if (!n) return 0;
	switch (n->type) {
	case NODE_PRINT: case NODE_PRINT0: case NODE_PRINTX:
	case NODE_LS: case NODE_PRINTF:
	case NODE_FPRINT: case NODE_FPRINT0: case NODE_FLS: case NODE_FPRINTF:
	case NODE_EXEC: case NODE_EXECDIR:
	case NODE_OK: case NODE_OKDIR:
	case NODE_DELETE:
	case NODE_QUIT:
		return 1;
	default:
		return has_action(n->left) || has_action(n->right);
	}
}

/* ── Flush all pending exec+ batches ── */
void flush_batches(node_t *n)
{
	if (!n) return;
	if ((n->type == NODE_EXEC || n->type == NODE_EXECDIR) && n->exec_plus)
		exec_batch_flush(n);
	flush_batches(n->left);
	flush_batches(n->right);
}

/* ── Free AST ── */
void free_node(node_t *n)
{
	if (!n) return;
	free_node(n->left);
	free_node(n->right);
	if (n->sval) free(n->sval);
	if (n->re_compiled) regfree(&n->re);
	if (n->exec_argv) free(n->exec_argv);
	if (n->batch_args) {
		for (int i = 0; i < n->batch_count; i++)
			free(n->batch_args[i]);
		free(n->batch_args);
	}
	if (n->out_fp) fclose(n->out_fp);
	free(n);
}
