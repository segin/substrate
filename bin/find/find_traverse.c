/*
 * find_traverse.c - directory traversal engine
 */
#include "find.h"

/* ── Loop detection (ancestor dev/ino stack) ── */
#define MAX_LOOP_DEPTH 4096
static struct { dev_t dev; ino_t ino; } g_ancestors[MAX_LOOP_DEPTH];
static int g_ancestor_count = 0;

static int qsort_strcmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

void traverse(const char *path, node_t *expr, int depth,
              int is_cmdline, dev_t root_dev)
{
	/* Depth limits */
	if (g_maxdepth >= 0 && depth > g_maxdepth) return;

	entry_t e;
	memset(&e, 0, sizeof(e));
	e.path = path;
	e.depth = depth;
	e.is_cmdline = is_cmdline;

	/* Basename */
	const char *sl = strrchr(path, '/');
	e.name = sl ? sl + 1 : path;

	/* Stat the entry */
	if (do_stat(&e) < 0) {
		if (!g_ignore_race || errno != ENOENT)
			fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
		g_exit_status = 1;
		return;
	}

	/* xdev check */
	if (g_xdev && !is_cmdline && e.st.st_dev != root_dev)
		return;

	/* Loop detection for directories */
	int is_dir = S_ISDIR(e.st.st_mode);
	if (is_dir) {
		for (int i = 0; i < g_ancestor_count; i++) {
			if (g_ancestors[i].dev == e.st.st_dev &&
			    g_ancestors[i].ino == e.st.st_ino) {
				fprintf(stderr, "find: filesystem loop detected: '%s'\n", path);
				return;
			}
		}
	}

	/* Pre-order: evaluate before descending (unless -depth) */
	int pruned = 0;
	if (!g_depth_first) {
		if (g_mindepth < 0 || depth >= g_mindepth) {
			g_pruned = 0;
			eval_node(expr, &e);
			if (g_pruned) pruned = 1;
		}
	}

	/* Descend into directories */
	if (is_dir && !pruned) {
		if (g_ancestor_count < MAX_LOOP_DEPTH) {
			g_ancestors[g_ancestor_count].dev = e.st.st_dev;
			g_ancestors[g_ancestor_count].ino = e.st.st_ino;
			g_ancestor_count++;
		}

		DIR *d = opendir(path);
		if (!d) {
			if (!g_ignore_race || errno != ENOENT)
				fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
			g_exit_status = 1;
		} else {
			/* Read all entries */
			char **entries = NULL;
			int entry_count = 0, entry_cap = 0;
			struct dirent *de;
			while ((de = readdir(d)) != NULL) {
				if (strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0)
					continue;
				if (entry_count >= entry_cap) {
					entry_cap = entry_cap ? entry_cap * 2 : 64;
					entries = realloc(entries, sizeof(char *) * entry_cap);
				}
				entries[entry_count++] = strdup(de->d_name);
			}
			closedir(d);

			/* Sort if requested */
			if (g_sorted && entry_count > 1)
				qsort(entries, entry_count, sizeof(char *), qsort_strcmp);

			/* Recurse */
			for (int i = 0; i < entry_count; i++) {
				size_t plen = strlen(path);
				size_t nlen = strlen(entries[i]);
				char *child = malloc(plen + nlen + 2);
				if (plen > 0 && path[plen - 1] == '/')
					snprintf(child, plen + nlen + 2, "%s%s", path, entries[i]);
				else
					snprintf(child, plen + nlen + 2, "%s/%s", path, entries[i]);
				traverse(child, expr, depth + 1, 0, root_dev);
				free(child);
				free(entries[i]);
			}
			free(entries);
		}

		g_ancestor_count--;
	}

	/* Post-order: evaluate after descending */
	if (g_depth_first) {
		if (g_mindepth < 0 || depth >= g_mindepth)
			eval_node(expr, &e);
	}
}
