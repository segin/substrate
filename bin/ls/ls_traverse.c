#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ls_print.h"
#include "ls_sort.h"
#include "ls_traverse.h"

#ifndef ELOOP
#define ELOOP 40
#endif

typedef struct {
    file_info_t *items;
    size_t len;
    size_t cap;
} file_vec_t;

typedef struct {
    char *path;
    struct stat st;
} dir_operand_t;

typedef struct {
    dir_operand_t *items;
    size_t len;
    size_t cap;
} dir_vec_t;

typedef struct {
    dev_t dev;
    ino_t ino;
} visit_key_t;

typedef struct {
    visit_key_t *items;
    size_t len;
    size_t cap;
} visit_set_t;

typedef struct {
    const ls_config_t *config;
    visit_set_t visited;
    int exit_code;
} ls_runtime_t;

typedef struct {
    const char *path;
    bool command_line_arg;
    bool print_header;
    dev_t root_dev;
    bool root_dev_valid;
} dir_list_ctx_t;

static void file_info_clear(file_info_t *info) {
    if (info == NULL) {
        return;
    }

    free(info->name);
    free(info->full_path);
    free(info->link_target);
    memset(info, 0, sizeof(*info));
}

static void file_vec_init(file_vec_t *vec) {
    memset(vec, 0, sizeof(*vec));
}

static void file_vec_free(file_vec_t *vec) {
    size_t i;

    if (vec == NULL) {
        return;
    }

    for (i = 0; i < vec->len; i++) {
        file_info_clear(&vec->items[i]);
    }

    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

static int file_vec_push(file_vec_t *vec, const file_info_t *item) {
    file_info_t *grown;
    size_t new_cap;

    if (vec->len == vec->cap) {
        new_cap = (vec->cap == 0) ? 32 : vec->cap * 2;
        grown = (file_info_t *)realloc(vec->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        vec->items = grown;
        vec->cap = new_cap;
    }

    vec->items[vec->len++] = *item;
    return 0;
}

static void dir_vec_init(dir_vec_t *vec) {
    memset(vec, 0, sizeof(*vec));
}

static void dir_vec_free(dir_vec_t *vec) {
    size_t i;

    if (vec == NULL) {
        return;
    }

    for (i = 0; i < vec->len; i++) {
        free(vec->items[i].path);
    }

    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

static int dir_vec_push(dir_vec_t *vec, const char *path, const struct stat *st) {
    dir_operand_t *grown;
    size_t new_cap;

    if (vec->len == vec->cap) {
        new_cap = (vec->cap == 0) ? 16 : vec->cap * 2;
        grown = (dir_operand_t *)realloc(vec->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        vec->items = grown;
        vec->cap = new_cap;
    }

    vec->items[vec->len].path = strdup(path);
    if (vec->items[vec->len].path == NULL) {
        return -1;
    }
    vec->items[vec->len].st = *st;
    vec->len++;
    return 0;
}

static void runtime_init(ls_runtime_t *rt, const ls_config_t *config) {
    memset(rt, 0, sizeof(*rt));
    rt->config = config;
}

static void runtime_free(ls_runtime_t *rt) {
    free(rt->visited.items);
    memset(rt, 0, sizeof(*rt));
}

static void set_exit_code(ls_runtime_t *rt, int code) {
    if (code > rt->exit_code) {
        rt->exit_code = code;
    }
}

static void warn_errno(ls_runtime_t *rt, const char *what, const char *path, int err, bool serious) {
    fprintf(stderr, "ls: %s '%s': %s\n", what, path, strerror(err));
    set_exit_code(rt, serious ? LS_EXIT_SERIOUS : LS_EXIT_MINOR);
}

static bool needs_metadata_stat(const ls_config_t *config) {
    if (config->long_fmt || config->show_blocks || config->inode) return true;
    if (config->sort_size || config->sort_time || config->dirs_first) return true;
    if (config->dereference || config->recursive || config->one_file_system) return true;
    if (config->classify || config->file_type || config->slash_dirs) return true;
    if (config->color != LS_COLOR_NEVER || config->list_xattr_names) return true;
    return false;
}

static bool visit_set_contains(const visit_set_t *set, dev_t dev, ino_t ino) {
    size_t i;

    for (i = 0; i < set->len; i++) {
        if (set->items[i].dev == dev && set->items[i].ino == ino) {
            return true;
        }
    }

    return false;
}

static int visit_set_add(visit_set_t *set, dev_t dev, ino_t ino) {
    visit_key_t *grown;
    size_t new_cap;

    if (visit_set_contains(set, dev, ino)) {
        return 0;
    }

    if (set->len == set->cap) {
        new_cap = (set->cap == 0) ? 32 : set->cap * 2;
        grown = (visit_key_t *)realloc(set->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        set->items = grown;
        set->cap = new_cap;
    }

    set->items[set->len].dev = dev;
    set->items[set->len].ino = ino;
    set->len++;
    return 0;
}

static char *path_join(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    size_t need = dir_len + 1 + name_len + 1;
    char *out = (char *)malloc(need);

    if (out == NULL) {
        return NULL;
    }

    if (dir_len == 1 && dir[0] == '/') {
        snprintf(out, need, "/%s", name);
    } else if (dir_len > 0 && dir[dir_len - 1] == '/') {
        snprintf(out, need, "%s%s", dir, name);
    } else {
        snprintf(out, need, "%s/%s", dir, name);
    }

    return out;
}

static char *readlink_dup(const char *path) {
    size_t cap = 128;
    char *buf = (char *)malloc(cap);

    if (buf == NULL) {
        return NULL;
    }

    for (;;) {
        char *grown;
        ssize_t n = readlink(path, buf, cap - 1);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        if ((size_t)n < cap - 1) {
            buf[n] = '\0';
            return buf;
        }

        cap *= 2;
        if (cap > 65536) {
            free(buf);
            return NULL;
        }

        grown = (char *)realloc(buf, cap);
        if (grown == NULL) {
            free(buf);
            return NULL;
        }
        buf = grown;
    }
}

static bool match_pattern(const char *pattern, const char *name) {
    return pattern != NULL && fnmatch(pattern, name, 0) == 0;
}

static bool should_include_name(const char *name, const ls_config_t *config) {
    if (!config->all && name[0] == '.') {
        if (!config->almost_all) {
            return false;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            return false;
        }
    }

    if (match_pattern(config->ignore_pattern, name)) {
        return false;
    }

    if (config->recursive && match_pattern(config->hide_pattern, name)) {
        return false;
    }

    return true;
}

static int init_basic_info(file_info_t *out, const char *display_name, const char *full_path, size_t index) {
    memset(out, 0, sizeof(*out));

    out->name = strdup(display_name);
    if (out->name == NULL) {
        return -1;
    }

    out->full_path = strdup(full_path);
    if (out->full_path == NULL) {
        file_info_clear(out);
        return -1;
    }

    out->input_index = index;
    return 0;
}

static int collect_name_only_info(const char *name, const char *full_path, size_t index, file_info_t *out) {
    if (init_basic_info(out, name, full_path, index) != 0) {
        return -1;
    }

    out->stat_ok = false;
    out->st.st_mode = S_IFREG;
    return 0;
}

static int collect_stat_info(ls_runtime_t *rt,
                             const char *display_name,
                             const char *full_path,
                             bool follow_symlink,
                             bool serious_on_error,
                             size_t index,
                             file_info_t *out) {
    struct stat lst;

    if (init_basic_info(out, display_name, full_path, index) != 0) {
        return -1;
    }

    if (lstat(full_path, &lst) != 0) {
        warn_errno(rt, "cannot access", full_path, errno, serious_on_error);
        file_info_clear(out);
        return 1;
    }

    out->st = lst;
    out->stat_ok = true;
    out->display_as_symlink = S_ISLNK(lst.st_mode);
    if (out->display_as_symlink) {
        out->link_target = readlink_dup(full_path);
    }

    if (follow_symlink && out->display_as_symlink) {
        struct stat st;

        if (stat(full_path, &st) == 0) {
            out->st = st;
            out->display_as_symlink = false;
            out->dangling_link = false;
            out->symlink_loop = false;
        } else {
            out->stat_error = errno;
            out->dangling_link = (errno == ENOENT);
            out->symlink_loop = (errno == ELOOP);
            if (errno == ELOOP) {
                warn_errno(rt, "cannot dereference", full_path, errno, false);
            }
        }
    }

    return 0;
}

static int collect_entry(ls_runtime_t *rt,
                         const char *display_name,
                         const char *full_path,
                         bool follow_symlink,
                         bool serious_on_error,
                         size_t index,
                         bool want_stat,
                         file_info_t *out) {
    if (want_stat) {
        return collect_stat_info(rt, display_name, full_path, follow_symlink, serious_on_error, index, out);
    }
    return collect_name_only_info(display_name, full_path, index, out);
}

static int append_directory_entry(ls_runtime_t *rt,
                                  file_vec_t *entries,
                                  const char *dir_path,
                                  const char *entry_name,
                                  size_t index,
                                  bool want_stat) {
    file_info_t info;
    char *full_path;
    int rc;

    full_path = path_join(dir_path, entry_name);
    if (full_path == NULL) {
        return -1;
    }

    rc = collect_entry(rt,
                       entry_name,
                       full_path,
                       rt->config->dereference,
                       false,
                       index,
                       want_stat,
                       &info);
    free(full_path);

    if (rc <= 0) {
        if (rc == 0 && file_vec_push(entries, &info) != 0) {
            file_info_clear(&info);
            return -1;
        }
        return rc;
    }

    return 1;
}

static int collect_directory_entries(ls_runtime_t *rt, const dir_list_ctx_t *ctx, file_vec_t *entries) {
    DIR *dir;
    struct dirent *ent;
    size_t index = 0;
    bool want_stat = needs_metadata_stat(rt->config);

    dir = opendir(ctx->path);
    if (dir == NULL) {
        warn_errno(rt, "cannot open directory", ctx->path, errno, ctx->command_line_arg);
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        int rc;

        if (!should_include_name(ent->d_name, rt->config)) {
            continue;
        }

        rc = append_directory_entry(rt, entries, ctx->path, ent->d_name, index, want_stat);
        if (rc < 0) {
            closedir(dir);
            return -1;
        }
        if (rc == 0) {
            index++;
        }
    }

    closedir(dir);
    return 0;
}

static int list_directory(ls_runtime_t *rt, const dir_list_ctx_t *ctx);

static int recurse_into_subdirs(ls_runtime_t *rt, const dir_list_ctx_t *ctx, const file_vec_t *entries) {
    size_t i;

    for (i = 0; i < entries->len; i++) {
        dir_list_ctx_t child;
        const file_info_t *info = &entries->items[i];

        if (!S_ISDIR(info->st.st_mode)) {
            continue;
        }
        if (strcmp(info->name, ".") == 0 || strcmp(info->name, "..") == 0) {
            continue;
        }
        if (rt->config->one_file_system && ctx->root_dev_valid && info->st.st_dev != ctx->root_dev) {
            continue;
        }
        if (visit_set_contains(&rt->visited, info->st.st_dev, info->st.st_ino)) {
            fprintf(stderr, "ls: skipping directory '%s': filesystem loop detected\n", info->full_path);
            set_exit_code(rt, LS_EXIT_MINOR);
            continue;
        }
        if (visit_set_add(&rt->visited, info->st.st_dev, info->st.st_ino) != 0) {
            return -1;
        }

        putchar('\n');
        child.path = info->full_path;
        child.command_line_arg = false;
        child.print_header = true;
        child.root_dev = ctx->root_dev;
        child.root_dev_valid = ctx->root_dev_valid;

        if (list_directory(rt, &child) != 0) {
            return -1;
        }
    }

    return 0;
}

static int list_directory(ls_runtime_t *rt, const dir_list_ctx_t *ctx) {
    file_vec_t entries;

    file_vec_init(&entries);

    if (collect_directory_entries(rt, ctx, &entries) != 0) {
        file_vec_free(&entries);
        return -1;
    }

    if (ctx->print_header) {
        printf("%s:\n", ctx->path);
    }

    ls_sort_entries(entries.items, entries.len, rt->config);
    ls_print_list(ctx->path, entries.items, entries.len, rt->config, true);

    if (rt->config->recursive && !rt->config->directory) {
        if (recurse_into_subdirs(rt, ctx, &entries) != 0) {
            file_vec_free(&entries);
            return -1;
        }
    }

    file_vec_free(&entries);
    return 0;
}

static int classify_operand(ls_runtime_t *rt,
                            file_vec_t *files,
                            dir_vec_t *dirs,
                            const char *path,
                            size_t input_index) {
    file_info_t info;
    bool follow_arg = rt->config->dereference || rt->config->dereference_args;
    int rc;

    rc = collect_entry(rt, path, path, follow_arg, true, input_index, true, &info);
    if (rc != 0) {
        if (rc < 0) {
            set_exit_code(rt, LS_EXIT_SERIOUS);
            return -1;
        }
        return 0;
    }

    if (S_ISDIR(info.st.st_mode) && !rt->config->directory) {
        if (dir_vec_push(dirs, path, &info.st) != 0) {
            file_info_clear(&info);
            set_exit_code(rt, LS_EXIT_SERIOUS);
            return -1;
        }
        file_info_clear(&info);
        return 0;
    }

    if (file_vec_push(files, &info) != 0) {
        file_info_clear(&info);
        set_exit_code(rt, LS_EXIT_SERIOUS);
        return -1;
    }

    return 0;
}

static int classify_operands(ls_runtime_t *rt, char **paths, int path_count, file_vec_t *files, dir_vec_t *dirs) {
    size_t i;

    for (i = 0; i < (size_t)path_count; i++) {
        if (classify_operand(rt, files, dirs, paths[i], i) != 0) {
            return -1;
        }
    }

    return 0;
}

static int print_file_operands(ls_runtime_t *rt, file_vec_t *files) {
    if (files->len == 0) {
        return 0;
    }

    ls_sort_entries(files->items, files->len, rt->config);
    ls_print_list(NULL, files->items, files->len, rt->config, false);
    return 0;
}

static int print_directory_operands(ls_runtime_t *rt, const file_vec_t *files, const dir_vec_t *dirs) {
    size_t i;

    for (i = 0; i < dirs->len; i++) {
        dir_list_ctx_t ctx;
        bool print_header = (dirs->len > 1) || (files->len > 0) || rt->config->recursive;

        if (i > 0 || (i == 0 && files->len > 0)) {
            putchar('\n');
        }

        if (visit_set_add(&rt->visited, dirs->items[i].st.st_dev, dirs->items[i].st.st_ino) != 0) {
            set_exit_code(rt, LS_EXIT_SERIOUS);
            return -1;
        }

        ctx.path = dirs->items[i].path;
        ctx.command_line_arg = true;
        ctx.print_header = print_header;
        ctx.root_dev = dirs->items[i].st.st_dev;
        ctx.root_dev_valid = true;

        if (list_directory(rt, &ctx) != 0) {
            set_exit_code(rt, LS_EXIT_SERIOUS);
            return -1;
        }
    }

    return 0;
}

int ls_run(const ls_config_t *config, char **paths, int path_count) {
    ls_runtime_t rt;
    file_vec_t files;
    dir_vec_t dirs;
    const char *implicit_paths[] = {"."};
    int exit_code;

    runtime_init(&rt, config);
    file_vec_init(&files);
    dir_vec_init(&dirs);

    if (path_count == 0) {
        paths = (char **)implicit_paths;
        path_count = 1;
    }

    if (classify_operands(&rt, paths, path_count, &files, &dirs) != 0) {
        goto done;
    }

    if (print_file_operands(&rt, &files) != 0) {
        goto done;
    }

    if (print_directory_operands(&rt, &files, &dirs) != 0) {
        goto done;
    }

done:
    exit_code = rt.exit_code;
    file_vec_free(&files);
    dir_vec_free(&dirs);
    runtime_free(&rt);
    return exit_code;
}
