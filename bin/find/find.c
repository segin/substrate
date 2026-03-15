#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#include <getopt.h>
#include <errno.h>
#include <libgen.h>

#define TYPE_DIR  1
#define TYPE_FILE 2
#define TYPE_CHR  3
#define TYPE_BLK  4
#define TYPE_FIFO 5
#define TYPE_LNK  6
#define TYPE_SOCK 7

typedef struct node {
	int type;
	char *pattern;
	struct node *left;
	struct node *right;
	char **argv;
	int argc;
} node_t;

typedef struct entry {
	char *path;
	char *name;
	struct stat st;
} entry_t;

int g_exit_status = 0;

static node_t *parse_expr(char **argv, int *idx, int argc);

static node_t *new_node(int type) {
	node_t *n = calloc(1, sizeof(node_t));
	if(!n) {
		perror("find: malloc");
		exit(1);
	}
	n->type = type;
	return n;
}

#define NODE_PRINT     1
#define NODE_NAME      2
#define NODE_TYPE      3
#define NODE_AND       4
#define NODE_OR        5
#define NODE_NOT       6
#define NODE_EXEC      7
#define NODE_EXECDIR   8

static node_t *parse_primary(char **argv, int *idx, int argc) {
	if(*idx >= argc) return NULL;

	if(strcmp(argv[*idx], "(") == 0) {
		(*idx)++;
		node_t *n = parse_expr(argv, idx, argc);
		if(*idx >= argc || strcmp(argv[*idx], ")") != 0) {
			fprintf(stderr, "find: expected ')'\n");
			exit(1);
		}
		(*idx)++;
		return n;
	}

	if(strcmp(argv[*idx], "!") == 0 || strcmp(argv[*idx], "-not") == 0) {
		(*idx)++;
		node_t *n = new_node(NODE_NOT);
		n->left = parse_primary(argv, idx, argc);
		return n;
	}

	if(strcmp(argv[*idx], "-name") == 0) {
		(*idx)++;
		if(*idx >= argc) {
			fprintf(stderr, "find: -name requires an argument\n");
			exit(1);
		}
		node_t *n = new_node(NODE_NAME);
		n->pattern = argv[(*idx)++];
		return n;
	}

	if(strcmp(argv[*idx], "-type") == 0) {
		(*idx)++;
		if(*idx >= argc) {
			fprintf(stderr, "find: -type requires an argument\n");
			exit(1);
		}
		node_t *n = new_node(NODE_TYPE);
		char t = argv[*idx][0];
		switch(t) {
			case 'd': n->type = NODE_TYPE; n->argc = TYPE_DIR; break;
			case 'f': n->type = NODE_TYPE; n->argc = TYPE_FILE; break;
			case 'c': n->type = NODE_TYPE; n->argc = TYPE_CHR; break;
			case 'b': n->type = NODE_TYPE; n->argc = TYPE_BLK; break;
			case 'p': n->type = NODE_TYPE; n->argc = TYPE_FIFO; break;
			case 'l': n->type = NODE_TYPE; n->argc = TYPE_LNK; break;
			case 's': n->type = NODE_TYPE; n->argc = TYPE_SOCK; break;
			default:
				fprintf(stderr, "find: unknown type %c\n", t);
				exit(1);
		}
		(*idx)++;
		return n;
	}

	if(strcmp(argv[*idx], "-print") == 0) {
		(*idx)++;
		return new_node(NODE_PRINT);
	}

	if(strcmp(argv[*idx], "-exec") == 0 || strcmp(argv[*idx], "-execdir") == 0) {
		int is_execdir = (strcmp(argv[*idx], "-execdir") == 0);
		(*idx)++;
		node_t *n = new_node(is_execdir ? NODE_EXECDIR : NODE_EXEC);
		int start = *idx;
		while(*idx < argc && strcmp(argv[*idx], ";") != 0) {
			(*idx)++;
		}
		if(*idx >= argc) {
			fprintf(stderr, "find: %s requires a terminating ';'\n", is_execdir ? "-execdir" : "-exec");
			exit(1);
		}
		n->argc = *idx - start;
		n->argv = malloc((n->argc + 1) * sizeof(char*));
		for(int i = 0; i < n->argc; i++) {
			n->argv[i] = argv[start + i];
		}
		n->argv[n->argc] = NULL;
		(*idx)++; // Skip ';'
		return n;
	}

	return NULL;
}

static node_t *parse_and(char **argv, int *idx, int argc) {
	node_t *left = parse_primary(argv, idx, argc);
	if(!left) return NULL;

	while(*idx < argc) {
		if(strcmp(argv[*idx], "-a") == 0 || strcmp(argv[*idx], "-and") == 0) {
			(*idx)++;
		} else if(strcmp(argv[*idx], "-o") == 0 || strcmp(argv[*idx], "-or") == 0 || strcmp(argv[*idx], ")") == 0) {
			break;
		}
		
		node_t *right = parse_primary(argv, idx, argc);
		if(!right) break;
		node_t *n = new_node(NODE_AND);
		n->left = left;
		n->right = right;
		left = n;
	}
	return left;
}

static node_t *parse_expr(char **argv, int *idx, int argc) {
	node_t *left = parse_and(argv, idx, argc);
	if(!left) return NULL;

	while(*idx < argc && (strcmp(argv[*idx], "-o") == 0 || strcmp(argv[*idx], "-or") == 0)) {
		(*idx)++;
		node_t *right = parse_and(argv, idx, argc);
		if(!right) {
			fprintf(stderr, "find: expected expression after -o\n");
			exit(1);
		}
		node_t *n = new_node(NODE_OR);
		n->left = left;
		n->right = right;
		left = n;
	}
	return left;
}

static int eval_node(node_t *n, entry_t *e) {
	if(!n) return 1;

	switch(n->type) {
		case NODE_PRINT:
			printf("%s\n", e->path);
			return 1;
		case NODE_NAME:
			return fnmatch(n->pattern, e->name, 0) == 0;
		case NODE_TYPE:
			switch(n->argc) {
				case TYPE_DIR: return S_ISDIR(e->st.st_mode);
				case TYPE_FILE: return S_ISREG(e->st.st_mode);
				case TYPE_CHR: return S_ISCHR(e->st.st_mode);
				case TYPE_BLK: return S_ISBLK(e->st.st_mode);
				case TYPE_FIFO: return S_ISFIFO(e->st.st_mode);
				case TYPE_LNK: return S_ISLNK(e->st.st_mode);
				case TYPE_SOCK: return S_ISSOCK(e->st.st_mode);
			}
			return 0;
		case NODE_AND:
			return eval_node(n->left, e) && eval_node(n->right, e);
		case NODE_OR:
			return eval_node(n->left, e) || eval_node(n->right, e);
		case NODE_NOT:
			return !eval_node(n->left, e);
		case NODE_EXEC:
		case NODE_EXECDIR: {
			char **argv = malloc((n->argc + 1) * sizeof(char*));
			char *path_to_use = e->path;
			char *old_cwd = NULL;

			if(n->type == NODE_EXECDIR) {
				// Secure PATH check for -execdir
				char *env_path = getenv("PATH");
				if(env_path) {
					char *dup = strdup(env_path);
					char *p = dup;
					char *tok;
					while((tok = strsep(&p, ":")) != NULL) {
						if(strcmp(tok, ".") == 0 || strcmp(tok, "") == 0 || tok[0] != '/') {
							fprintf(stderr, "find: The relative path '%s' is included "
								"in the PATH environment variable, which is "
								"insecure in combination with the -execdir action.\n", tok);
							free(dup);
							g_exit_status = 1;
							return(0);
						}
					}
					free(dup);
				}

				char *d_path = strdup(e->path);
				char *dir = dirname(d_path);
				old_cwd = getcwd(NULL, 0);
				if(chdir(dir) != 0) {
					perror("find: chdir");
					free(d_path);
					free(argv);
					return 0;
				}
				path_to_use = e->name;
				// Prefix with ./ if not already absolute or relative-prefixed
				if(path_to_use[0] != '/') {
					char *tmp = malloc(strlen(path_to_use) + 3);
					sprintf(tmp, "./%s", path_to_use);
					path_to_use = tmp;
				}
				free(d_path);
			}

			for(int i = 0; i < n->argc; i++) {
				if(strcmp(n->argv[i], "{}") == 0) {
					argv[i] = path_to_use;
				} else {
					argv[i] = n->argv[i];
				}
			}
			argv[n->argc] = NULL;

			pid_t pid = fork();
			if(pid == 0) {
				execvp(argv[0], argv);
				perror("find: execvp");
				exit(1);
			} else if(pid > 0) {
				int status;
				waitpid(pid, &status, 0);
				if(n->type == NODE_EXECDIR && path_to_use != e->name) {
					free(path_to_use);
				}
				if(old_cwd) {
					chdir(old_cwd);
					free(old_cwd);
				}
				free(argv);
				return WIFEXITED(status) && WEXITSTATUS(status) == 0;
			} else {
				perror("find: fork");
				if(old_cwd) free(old_cwd);
				free(argv);
				return 0;
			}
		}
	}
	return 0;
}

static void walk(char *path, node_t *root) {
	DIR *dir = opendir(path);
	if(!dir) {
		perror(path);
		g_exit_status = 1;
		return;
	}

	struct dirent *ent;
	while((ent = readdir(dir)) != NULL) {
		if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		char *full_path = malloc(strlen(path) + strlen(ent->d_name) + 2);
		sprintf(full_path, "%s/%s", path, ent->d_name);

		entry_t e;
		e.path = full_path;
		e.name = ent->d_name;
		if(lstat(full_path, &e.st) != 0) {
			perror(full_path);
			free(full_path);
			continue;
		}

		eval_node(root, &e);

		if(S_ISDIR(e.st.st_mode)) {
			walk(full_path, root);
		}
		free(full_path);
	}
	closedir(dir);
}

int main(int argc, char **argv) {
	if(argc < 2) {
		fprintf(stderr, "usage: find path [expression]\n");
		return 1;
	}

	char *start_path = argv[1];
	int idx = 2;
	node_t *root = parse_expr(argv, &idx, argc);

	if(!root) {
		root = new_node(NODE_PRINT);
	} else {
		// If no action primary, append -print
		// This is a simplification
	}

	entry_t e;
	e.path = start_path;
	e.name = start_path;
	if(lstat(start_path, &e.st) == 0) {
		eval_node(root, &e);
		if(S_ISDIR(e.st.st_mode)) {
			walk(start_path, root);
		}
	} else {
		perror(start_path);
		g_exit_status = 1;
	}

	return g_exit_status;
}
