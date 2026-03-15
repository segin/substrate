/*
 * find_parse.c - recursive descent expression parser and optimizer
 */
#include "find.h"

/* ── Forward declarations for recursive parser ── */
static node_t *parse_or(char **argv, int *idx, int argc);
static node_t *parse_and(char **argv, int *idx, int argc);
static node_t *parse_unary(char **argv, int *idx, int argc);
static node_t *parse_primary(char **argv, int *idx, int argc);

/* ── Primary parser ── */
static node_t *parse_primary(char **argv, int *idx, int argc)
{
	if (*idx >= argc) return NULL;
	const char *tok = argv[*idx];

	if (strcmp(tok, "(") == 0) {
		(*idx)++;
		node_t *n = parse_or(argv, idx, argc);
		if (*idx < argc && strcmp(argv[*idx], ")") == 0) (*idx)++;
		return n;
	}

	if (strcmp(tok, "!") == 0 || strcmp(tok, "-not") == 0) {
		(*idx)++;
		node_t *n = new_node(NODE_NOT);
		n->left = parse_unary(argv, idx, argc);
		return n;
	}

	/* Primaries requiring an argument */
	#define NEED_ARG() do { \
		if (*idx + 1 >= argc) { \
			fprintf(stderr, "find: %s requires an argument\n", tok); \
			exit(1); \
		} \
	} while (0)

	if (strcmp(tok, "-name") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_NAME);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-iname") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_INAME);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-path") == 0 || strcmp(tok, "-wholename") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PATH);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-ipath") == 0 || strcmp(tok, "-iwholename") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_IPATH);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-type") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_TYPE);
		n->type_char = argv[(*idx)++][0];
		return n;
	}
	if (strcmp(tok, "-xtype") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_XTYPE);
		n->type_char = argv[(*idx)++][0];
		return n;
	}
	if (strcmp(tok, "-perm") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PERM);
		const char *arg = argv[(*idx)++];
		if (arg[0] == '-') { n->perm_mode = 1; arg++; }
		else if (arg[0] == '/') { n->perm_mode = 2; arg++; }
		else if (arg[0] == '+') { n->perm_mode = 2; arg++; } /* legacy GNU */
		else { n->perm_mode = 0; }
		n->mode_val = parse_mode_str(arg);
		return n;
	}
	if (strcmp(tok, "-links") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_LINKS);
		n->cmp = parse_cmp(argv[*idx], &n->ival);
		(*idx)++;
		return n;
	}
	if (strcmp(tok, "-user") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_USER);
		const char *arg = argv[(*idx)++];
		if (isdigit((unsigned char)arg[0]))
			n->ival = strtol(arg, NULL, 10);
		else
			n->sval = strdup(arg);
		return n;
	}
	if (strcmp(tok, "-group") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_GROUP);
		const char *arg = argv[(*idx)++];
		if (isdigit((unsigned char)arg[0]))
			n->ival = strtol(arg, NULL, 10);
		else
			n->sval = strdup(arg);
		return n;
	}
	if (strcmp(tok, "-size") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_SIZE);
		n->sval = strdup(argv[*idx]);
		n->cmp = parse_cmp(argv[*idx], &n->ival);
		(*idx)++;
		return n;
	}
	if (strcmp(tok, "-newer") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_NEWER);
		struct stat rs;
		if (stat(argv[*idx], &rs) < 0) {
			fprintf(stderr, "find: '%s': %s\n", argv[*idx], strerror(errno));
			exit(1);
		}
		n->newer_ts.tv_sec = rs.st_mtime;
		(*idx)++;
		return n;
	}

	/* Timestamp tests */
	#define TIME_PRIMARY(NAME, NTYPE) \
		if (strcmp(tok, NAME) == 0) { \
			NEED_ARG(); (*idx)++; \
			node_t *n = new_node(NTYPE); \
			n->cmp = parse_cmp(argv[*idx], &n->ival); \
			(*idx)++; \
			return n; \
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
	if (strcmp(tok, "-empty") == 0) { (*idx)++; return new_node(NODE_EMPTY); }
	if (strcmp(tok, "-true") == 0)  { (*idx)++; return new_node(NODE_TRUE); }
	if (strcmp(tok, "-false") == 0) { (*idx)++; return new_node(NODE_FALSE); }

	/* GNU access tests */
	if (strcmp(tok, "-readable") == 0)   { (*idx)++; return new_node(NODE_READABLE); }
	if (strcmp(tok, "-writable") == 0)   { (*idx)++; return new_node(NODE_WRITABLE); }
	if (strcmp(tok, "-executable") == 0) { (*idx)++; return new_node(NODE_EXECUTABLE); }

	/* Actions */
	if (strcmp(tok, "-print") == 0)  { (*idx)++; return new_node(NODE_PRINT); }
	if (strcmp(tok, "-print0") == 0) { (*idx)++; return new_node(NODE_PRINT0); }
	if (strcmp(tok, "-printx") == 0) { (*idx)++; return new_node(NODE_PRINTX); }
	if (strcmp(tok, "-ls") == 0)     { (*idx)++; return new_node(NODE_LS); }
	if (strcmp(tok, "-delete") == 0 || strcmp(tok, "-rm") == 0) {
		(*idx)++;
		g_depth_first = 1; /* -delete implies -depth */
		return new_node(NODE_DELETE);
	}
	if (strcmp(tok, "-prune") == 0)  { (*idx)++; return new_node(NODE_PRUNE); }
	if (strcmp(tok, "-quit") == 0)   { (*idx)++; return new_node(NODE_QUIT); }

	if (strcmp(tok, "-printf") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_PRINTF);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-fprint") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_FPRINT);
		n->out_fp = fopen(argv[*idx], "w");
		if (!n->out_fp) { perror(argv[*idx]); exit(1); }
		(*idx)++;
		return n;
	}
	if (strcmp(tok, "-fprint0") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_FPRINT0);
		n->out_fp = fopen(argv[*idx], "w");
		if (!n->out_fp) { perror(argv[*idx]); exit(1); }
		(*idx)++;
		return n;
	}
	if (strcmp(tok, "-fls") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_FLS);
		n->out_fp = fopen(argv[*idx], "w");
		if (!n->out_fp) { perror(argv[*idx]); exit(1); }
		(*idx)++;
		return n;
	}
	if (strcmp(tok, "-fprintf") == 0) {
		if (*idx + 2 >= argc) {
			fprintf(stderr, "find: -fprintf requires two arguments\n");
			exit(1);
		}
		(*idx)++;
		node_t *n = new_node(NODE_FPRINTF);
		n->out_fp = fopen(argv[*idx], "w");
		if (!n->out_fp) { perror(argv[*idx]); exit(1); }
		(*idx)++;
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}

	if (strcmp(tok, "-regex") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_REGEX);
		int flags = g_ere ? REG_EXTENDED : 0;
		if (regcomp(&n->re, argv[*idx], flags | REG_NOSUB) != 0) {
			fprintf(stderr, "find: invalid regex '%s'\n", argv[*idx]);
			exit(1);
		}
		n->re_compiled = 1;
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-iregex") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_IREGEX);
		int flags = (g_ere ? REG_EXTENDED : 0) | REG_ICASE;
		if (regcomp(&n->re, argv[*idx], flags | REG_NOSUB) != 0) {
			fprintf(stderr, "find: invalid regex '%s'\n", argv[*idx]);
			exit(1);
		}
		n->re_compiled = 1;
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}

	if (strcmp(tok, "-samefile") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_SAMEFILE);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}
	if (strcmp(tok, "-ilname") == 0) {
		NEED_ARG(); (*idx)++;
		node_t *n = new_node(NODE_ILNAME);
		n->sval = strdup(argv[(*idx)++]);
		return n;
	}

	/* -newerXY reference — GNU/BSD timestamp comparison */
	if (strncmp(tok, "-newer", 6) == 0 && strlen(tok) >= 8 &&
	    tok[6] != '\0' && tok[7] != '\0' && tok[8] == '\0') {
		NEED_ARG();
		node_t *n = new_node(NODE_NEWXY);
		n->newer_x = tok[6]; /* a, c, m, B */
		n->newer_y = tok[7]; /* a, c, m, B, t */
		(*idx)++;
		if (n->newer_y == 't') {
			/* -newerXt: compare against parsed time string */
			struct tm tm_val;
			memset(&tm_val, 0, sizeof(tm_val));
			if (strptime(argv[*idx], "%Y-%m-%d %H:%M:%S", &tm_val) ||
			    strptime(argv[*idx], "%Y-%m-%d", &tm_val) ||
			    strptime(argv[*idx], "%Y-%m-%dT%H:%M:%S", &tm_val)) {
				n->newer_ts.tv_sec = mktime(&tm_val);
			} else {
				n->newer_ts.tv_sec = (time_t)strtol(argv[*idx], NULL, 10);
			}
		} else {
			struct stat rs;
			if (stat(argv[*idx], &rs) < 0) {
				fprintf(stderr, "find: '%s': %s\n", argv[*idx], strerror(errno));
				exit(1);
			}
			switch (n->newer_y) {
			case 'a': n->newer_ts.tv_sec = rs.st_atime; break;
			case 'c': n->newer_ts.tv_sec = rs.st_ctime; break;
			default:  n->newer_ts.tv_sec = rs.st_mtime; break;
			}
		}
		(*idx)++;
		return n;
	}

	/* -exec / -execdir / -ok / -okdir */
	if (strcmp(tok, "-exec") == 0 || strcmp(tok, "-execdir") == 0 ||
	    strcmp(tok, "-ok") == 0 || strcmp(tok, "-okdir") == 0) {
		int is_ok = (tok[1] == 'o');
		int is_dir = (strstr(tok, "dir") != NULL);
		enum node_type nt;
		if (is_ok) nt = is_dir ? NODE_OKDIR : NODE_OK;
		else nt = is_dir ? NODE_EXECDIR : NODE_EXEC;

		(*idx)++;
		node_t *n = new_node(nt);
		n->exec_dir = is_dir;

		/* Collect argv until ; or + */
		int start = *idx;
		while (*idx < argc) {
			if (strcmp(argv[*idx], ";") == 0) {
				n->exec_plus = 0;
				break;
			}
			if (strcmp(argv[*idx], "+") == 0) {
				/* Check if previous arg was {} */
				if (*idx > start && strcmp(argv[*idx - 1], "{}") == 0) {
					n->exec_plus = 1;
					break;
				}
			}
			(*idx)++;
		}
		n->exec_argc = *idx - start;
		n->exec_argv = malloc(sizeof(char *) * n->exec_argc);
		for (int i = 0; i < n->exec_argc; i++)
			n->exec_argv[i] = argv[start + i];
		if (*idx < argc) (*idx)++; /* skip ; or + */
		return n;
	}

	/* ── Global modifiers: consumed here but not placed in AST as predicates ── */
	if (strcmp(tok, "-depth") == 0 || strcmp(tok, "-d") == 0) {
		g_depth_first = 1;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-xdev") == 0 || strcmp(tok, "-mount") == 0) {
		g_xdev = 1;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-maxdepth") == 0) {
		NEED_ARG(); (*idx)++;
		g_maxdepth = atoi(argv[(*idx)++]);
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-mindepth") == 0) {
		NEED_ARG(); (*idx)++;
		g_mindepth = atoi(argv[(*idx)++]);
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-follow") == 0) {
		g_deref = DEREF_ALWAYS;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-daystart") == 0) {
		g_daystart = 1;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-ignore_readdir_race") == 0) {
		g_ignore_race = 1;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-noignore_readdir_race") == 0) {
		g_ignore_race = 0;
		(*idx)++;
		return new_node(NODE_TRUE);
	}
	if (strcmp(tok, "-noleaf") == 0) {
		(*idx)++;
		return new_node(NODE_TRUE); /* silently accept */
	}
	if (strcmp(tok, "-warn") == 0 || strcmp(tok, "-nowarn") == 0) {
		(*idx)++;
		return new_node(NODE_TRUE);
	}

	#undef NEED_ARG

	fprintf(stderr, "find: unknown predicate '%s'\n", tok);
	exit(1);
}

/* ── Recursive descent grammar ── */

static node_t *parse_unary(char **argv, int *idx, int argc)
{
	return parse_primary(argv, idx, argc);
}

static node_t *parse_and(char **argv, int *idx, int argc)
{
	node_t *left = parse_unary(argv, idx, argc);
	while (*idx < argc) {
		const char *tok = argv[*idx];
		/* Explicit AND */
		if (strcmp(tok, "-a") == 0 || strcmp(tok, "-and") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_AND);
			n->left = left;
			n->right = parse_unary(argv, idx, argc);
			left = n;
		}
		/* Implicit AND: next token is a primary but not OR/close-paren */
		else if (strcmp(tok, "-o") != 0 && strcmp(tok, "-or") != 0 &&
		         strcmp(tok, ")") != 0 && strcmp(tok, ",") != 0) {
			node_t *n = new_node(NODE_AND);
			n->left = left;
			n->right = parse_unary(argv, idx, argc);
			left = n;
		}
		else break;
	}
	return left;
}

static node_t *parse_or(char **argv, int *idx, int argc)
{
	node_t *left = parse_and(argv, idx, argc);
	while (*idx < argc) {
		if (strcmp(argv[*idx], "-o") == 0 || strcmp(argv[*idx], "-or") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_OR);
			n->left = left;
			n->right = parse_and(argv, idx, argc);
			left = n;
		} else if (strcmp(argv[*idx], ",") == 0) {
			(*idx)++;
			node_t *n = new_node(NODE_COMMA);
			n->left = left;
			n->right = parse_and(argv, idx, argc);
			left = n;
		} else break;
	}
	return left;
}

/* ── Public parser entry point ── */
node_t *parse_expr(char **argv, int *idx, int argc)
{
	return parse_or(argv, idx, argc);
}

/* ── Optimizer: reorder pure predicates in AND chains ── */

static int is_pure_test(node_t *n)
{
	if (!n) return 0;
	switch (n->type) {
	case NODE_NAME: case NODE_INAME: case NODE_PATH: case NODE_IPATH:
	case NODE_WHOLENAME: case NODE_TYPE: case NODE_XTYPE:
	case NODE_PERM: case NODE_LINKS: case NODE_USER: case NODE_GROUP:
	case NODE_SIZE: case NODE_NEWER: case NODE_NEWXY:
	case NODE_ATIME: case NODE_MTIME: case NODE_CTIME:
	case NODE_AMIN: case NODE_MMIN: case NODE_CMIN:
	case NODE_INUM: case NODE_EMPTY: case NODE_FSTYPE:
	case NODE_REGEX: case NODE_IREGEX:
	case NODE_READABLE: case NODE_WRITABLE: case NODE_EXECUTABLE:
	case NODE_SAMEFILE: case NODE_ILNAME:
	case NODE_TRUE: case NODE_FALSE:
		return 1;
	default:
		return 0;
	}
}

static int is_name_test(node_t *n)
{
	if (!n) return 0;
	switch (n->type) {
	case NODE_NAME: case NODE_INAME: case NODE_PATH: case NODE_IPATH:
	case NODE_WHOLENAME: case NODE_TYPE:
	case NODE_TRUE: case NODE_FALSE:
		return 1;
	default:
		return 0;
	}
}

/* Cost heuristic: lower = should be tested first */
static int node_cost(node_t *n, int opt_level)
{
	if (!n) return 100;
	if (opt_level >= 1 && is_name_test(n)) return 1;   /* filename tests are cheapest */
	if (opt_level >= 2 && is_pure_test(n)) return 10;   /* pure tests before actions */
	if (has_action(n)) return 50;                        /* actions are expensive */
	return 30;
}

/*
 * Reorder AND chains: within a left-associative AND tree, move cheaper
 * nodes to the left so short-circuit evaluation skips expensive actions.
 * Only reorders within AND; never crosses OR boundaries.
 */
node_t *optimize_ast(node_t *n, int opt_level)
{
	if (!n || opt_level == 0) return n;

	/* Recurse into children first */
	n->left = optimize_ast(n->left, opt_level);
	n->right = optimize_ast(n->right, opt_level);

	/* For AND nodes: if right child is cheaper than left, swap them.
	 * This is safe because both sides are pure tests (no side effects)
	 * or because we're only moving pure tests before impure ones. */
	if (n->type == NODE_AND) {
		int cost_l = node_cost(n->left, opt_level);
		int cost_r = node_cost(n->right, opt_level);
		/* Only swap if right is cheaper AND the cheaper one is pure */
		if (cost_r < cost_l && is_pure_test(n->right)) {
			node_t *tmp = n->left;
			n->left = n->right;
			n->right = tmp;
		}
	}

	return n;
}
