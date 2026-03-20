/*
 * find_main.c - main entry, startup option parsing, orchestration
 *
 * Phase 1: Startup options (-H, -L, -P, -E, -s, -x, -X, -O, -f)
 * Phase 2: Starting points (non-option arguments before expression)
 * Phase 3: Expression parsing + optimizer pass
 * Phase 4: Traversal of each starting point
 */
#include "find.h"

/* ── Global variable definitions ── */
enum deref_mode g_deref = DEREF_NONE;
int g_depth_first = 0;
int g_xdev = 0;
int g_sorted = 0;
int g_maxdepth = -1;
int g_mindepth = -1;
int g_daystart = 0;
int g_ere = 0;
int g_ignore_race = 0;
int g_exit_status = 0;
int g_pruned = 0;
int g_opt_level = 1;
unsigned g_debug = DEBUG_NONE;
time_t g_now;
time_t g_daystart_time;

/* Parse -D flag argument */
static unsigned parse_debug_flags(const char *arg)
{
	unsigned flags = 0;
	if (strcmp(arg, "help") == 0) {
		fprintf(stderr, "Valid arguments for -D:\n"
		        "  tree   - show parsed expression tree\n"
		        "  stat   - trace stat calls\n"
		        "  opt    - show optimizer actions\n"
		        "  rates  - predicate success rates\n"
		        "  exec   - trace exec calls\n"
		        "  all    - all of the above\n"
		        "  help   - print this message\n");
		exit(0);
	}
	/* Parse comma-separated list */
	char *dup = strdup(arg);
	char *tok = strtok(dup, ",");
	while (tok) {
		if (strcmp(tok, "tree") == 0)  flags |= DEBUG_TREE;
		else if (strcmp(tok, "stat") == 0)  flags |= DEBUG_STAT;
		else if (strcmp(tok, "opt") == 0)   flags |= DEBUG_OPT;
		else if (strcmp(tok, "rates") == 0) flags |= DEBUG_RATES;
		else if (strcmp(tok, "exec") == 0)  flags |= DEBUG_EXEC;
		else if (strcmp(tok, "all") == 0)   flags |= DEBUG_ALL;
		else {
			fprintf(stderr, "find: unknown debug flag '%s' "
			        "(use -D help for list)\n", tok);
			free(dup);
			exit(1);
		}
		tok = strtok(NULL, ",");
	}
	free(dup);
	return flags;
}

/* Print AST tree for -D tree */
static void print_tree(node_t *n, int indent)
{
	if (!n) return;
	for (int i = 0; i < indent; i++) fputs("  ", stderr);

	static const char *type_names[] = {
		[NODE_AND] = "AND", [NODE_OR] = "OR", [NODE_NOT] = "NOT",
		[NODE_COMMA] = "COMMA", [NODE_NAME] = "-name", [NODE_INAME] = "-iname",
		[NODE_PATH] = "-path", [NODE_IPATH] = "-ipath",
		[NODE_WHOLENAME] = "-wholename", [NODE_TYPE] = "-type",
		[NODE_PERM] = "-perm", [NODE_LINKS] = "-links",
		[NODE_USER] = "-user", [NODE_GROUP] = "-group",
		[NODE_SIZE] = "-size", [NODE_NEWER] = "-newer",
		[NODE_NEWXY] = "-newerXY",
		[NODE_ATIME] = "-atime", [NODE_MTIME] = "-mtime",
		[NODE_CTIME] = "-ctime", [NODE_AMIN] = "-amin",
		[NODE_MMIN] = "-mmin", [NODE_CMIN] = "-cmin",
		[NODE_INUM] = "-inum", [NODE_EMPTY] = "-empty",
		[NODE_FSTYPE] = "-fstype",
		[NODE_REGEX] = "-regex", [NODE_IREGEX] = "-iregex",
		[NODE_READABLE] = "-readable", [NODE_WRITABLE] = "-writable",
		[NODE_EXECUTABLE] = "-executable",
		[NODE_SAMEFILE] = "-samefile", [NODE_XTYPE] = "-xtype",
		[NODE_ILNAME] = "-ilname",
		[NODE_PRINT] = "-print", [NODE_PRINT0] = "-print0",
		[NODE_PRINTX] = "-printx", [NODE_LS] = "-ls",
		[NODE_PRINTF] = "-printf",
		[NODE_EXEC] = "-exec", [NODE_EXECDIR] = "-execdir",
		[NODE_OK] = "-ok", [NODE_OKDIR] = "-okdir",
		[NODE_DELETE] = "-delete", [NODE_PRUNE] = "-prune",
		[NODE_QUIT] = "-quit",
		[NODE_FPRINT] = "-fprint", [NODE_FPRINT0] = "-fprint0",
		[NODE_FLS] = "-fls", [NODE_FPRINTF] = "-fprintf",
		[NODE_TRUE] = "-true", [NODE_FALSE] = "-false",
	};
	const char *name = "???";
	if ((unsigned)n->type < sizeof(type_names) / sizeof(type_names[0]) &&
	    type_names[n->type])
		name = type_names[n->type];

	fprintf(stderr, "%s", name);
	if (n->sval) fprintf(stderr, " '%s'", n->sval);
	if (n->type == NODE_TYPE || n->type == NODE_XTYPE)
		fprintf(stderr, " '%c'", n->type_char);
	fputc('\n', stderr);
	print_tree(n->left, indent + 1);
	print_tree(n->right, indent + 1);
}

/* Check if AST contains a given node type */
static int has_node_type(node_t *n, enum node_type type)
{
	if (!n) return 0;
	if (n->type == type) return 1;
	return has_node_type(n->left, type) || has_node_type(n->right, type);
}

int main(int argc, char **argv)
{
	g_now = time(NULL);

	/* Daystart: beginning of today */
	{
		struct tm *t = localtime(&g_now);
		t->tm_hour = t->tm_min = t->tm_sec = 0;
		g_daystart_time = mktime(t);
	}

	/* Phase 1: classify argv into startup options, paths, expression */
	int i = 1;
	while (i < argc) {
		if (strcmp(argv[i], "-H") == 0) {
			g_deref = DEREF_CMDLINE; i++;
		} else if (strcmp(argv[i], "-L") == 0 ||
		           strcmp(argv[i], "-follow") == 0 ||
		           strcmp(argv[i], "-h") == 0) {
			g_deref = DEREF_ALWAYS; i++;
		} else if (strcmp(argv[i], "-P") == 0) {
			g_deref = DEREF_NONE; i++;
		} else if (strcmp(argv[i], "-E") == 0) {
			g_ere = 1; i++;
		} else if (strcmp(argv[i], "-s") == 0) {
			g_sorted = 1; i++;
		} else if (strcmp(argv[i], "-x") == 0) {
			g_xdev = 1; i++;
		} else if (strcmp(argv[i], "-X") == 0) {
			/* OpenBSD: safe xargs mode */
			i++;
		} else if (strncmp(argv[i], "-O", 2) == 0 &&
		           argv[i][2] >= '0' && argv[i][2] <= '3') {
			g_opt_level = argv[i][2] - '0'; i++;
		} else if (strcmp(argv[i], "-D") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: -D requires an argument\n");
				exit(1);
			}
			i++;
			g_debug = parse_debug_flags(argv[i]); i++;
		} else if (strcmp(argv[i], "-regextype") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: -regextype requires an argument\n");
				exit(1);
			}
			i++;
			if (strcmp(argv[i], "posix-basic") == 0 ||
			    strcmp(argv[i], "sed") == 0)
				g_ere = 0;
			else if (strcmp(argv[i], "posix-extended") == 0 ||
			         strcmp(argv[i], "posix-egrep") == 0 ||
			         strcmp(argv[i], "egrep") == 0)
				g_ere = 1;
			else {
				fprintf(stderr, "find: unknown regex type '%s'\n"
				        "Valid types: posix-basic, posix-extended, "
				        "sed, egrep, posix-egrep\n", argv[i]);
				exit(1);
			}
			i++;
		} else if (strcmp(argv[i], "-f") == 0) {
			/* FreeBSD: -f path (path is next arg) */
			i++; /* skip -f, the path is left as a starting point */
			break;
		} else {
			break;
		}
	}

	/* Phase 2: starting points = args that don't start with - ( ! or , */
	int path_start = i;
	const char *files0_from = NULL;
	/* Check for -files0-from in the expression area */
	for (int fi = i; fi < argc; fi++) {
		if (strcmp(argv[fi], "-files0-from") == 0 && fi + 1 < argc) {
			files0_from = argv[fi + 1];
			/* Remove -files0-from and its argument from argv */
			for (int j = fi; j + 2 < argc; j++)
				argv[j] = argv[j + 2];
			argc -= 2;
			break;
		}
	}

	while (i < argc && argv[i][0] != '-' && argv[i][0] != '(' &&
	       argv[i][0] != '!' && argv[i][0] != ',') {
		i++;
	}
	int path_end = i;

	/* Read paths from -files0-from file or use argv paths */
	char *default_paths[] = { "." };
	char **paths;
	int path_count = 0;
	char **dyn_paths = NULL;

	if (files0_from) {
		/* Cannot combine -files0-from with explicit starting points */
		if (path_end != path_start) {
			fprintf(stderr, "find: paths must either be listed on the "
			        "command line, or specified via -files0-from, not both\n");
			exit(1);
		}

		FILE *fp;
		if (strcmp(files0_from, "-") == 0)
			fp = stdin;
		else {
			fp = fopen(files0_from, "r");
			if (!fp) {
				fprintf(stderr, "find: cannot open '%s': %s\n",
				        files0_from, strerror(errno));
				exit(1);
			}
		}

		int cap = 0;
		char buf[PATH_MAX];
		int bi = 0;
		int ch;
		while ((ch = fgetc(fp)) != EOF) {
			if (ch == '\0') {
				buf[bi] = '\0';
				if (bi > 0) {
					if (path_count >= cap) {
						cap = cap ? cap * 2 : 64;
						dyn_paths = realloc(dyn_paths, sizeof(char *) * cap);
					}
					dyn_paths[path_count++] = strdup(buf);
				}
				bi = 0;
			} else if (bi < (int)sizeof(buf) - 1) {
				buf[bi++] = (char)ch;
			}
		}
		/* Handle trailing entry without NUL */
		if (bi > 0) {
			buf[bi] = '\0';
			if (path_count >= cap) {
				cap = cap ? cap * 2 : 64;
				dyn_paths = realloc(dyn_paths, sizeof(char *) * cap);
			}
			dyn_paths[path_count++] = strdup(buf);
		}
		if (fp != stdin) fclose(fp);
		paths = dyn_paths;
	} else if (path_end == path_start) {
		paths = default_paths;
		path_count = 1;
	} else {
		paths = argv + path_start;
		path_count = path_end - path_start;
	}

	/* Phase 3: expression */
	int expr_start = i;
	node_t *expr = NULL;
	if (expr_start < argc) {
		int idx = expr_start;
		expr = parse_expr(argv, &idx, argc);
	}

	/* Implicit -print if no action in expression */
	/* REQ-FIND-101: reject -delete combined with -L (follow mode) */
	if (g_deref == DEREF_ALWAYS && has_node_type(expr, NODE_DELETE)) {
		fprintf(stderr, "find: the -delete action is not compatible "
		        "with -L (follow symlinks); use -P instead\n");
		exit(1);
	}

	if (!has_action(expr)) {
		node_t *print_node = new_node(NODE_PRINT);
		if (expr) {
			node_t *and_node = new_node(NODE_AND);
			and_node->left = expr;
			and_node->right = print_node;
			expr = and_node;
		} else {
			expr = print_node;
		}
	}

	/* Phase 3.5: optimizer pass */
	if (g_debug & DEBUG_OPT)
		fprintf(stderr, "find: optimizer level %d\n", g_opt_level);
	expr = optimize_ast(expr, g_opt_level);

	/* Debug: print expression tree */
	if (g_debug & DEBUG_TREE) {
		fprintf(stderr, "find: expression tree:\n");
		print_tree(expr, 1);
	}

	/* Phase 4: traverse each starting point */
	for (int p = 0; p < path_count; p++) {
		struct stat root_st;
		dev_t root_dev = 0;
		if (lstat(paths[p], &root_st) == 0)
			root_dev = root_st.st_dev;
		traverse(paths[p], expr, 0, 1, root_dev);
	}

	/* Flush any pending exec+ batches */
	flush_batches(expr);

	free_node(expr);

	/* Free dynamically allocated paths from -files0-from */
	if (dyn_paths) {
		for (int p = 0; p < path_count; p++)
			free(dyn_paths[p]);
		free(dyn_paths);
	}

	return g_exit_status;
}
