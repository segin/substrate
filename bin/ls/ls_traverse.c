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
    visit_key_t *keys;
    size_t len;
    size_t cap;
    int exit_code;
} ls_runtime_t;

static void file_info_clear(file_info_t *f) {
    if (f == NULL) {
        return;
    }
    free(f->name);
    free(f->full_path);
    free(f->link_target);
    memset(f, 0, sizeof(*f));
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
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static int file_vec_push(file_vec_t *vec, const file_info_t *item) {
    file_info_t *new_items;
    size_t ncap;

    if (vec->len == vec->cap) {
        ncap = vec->cap == 0 ? 32 : vec->cap * 2;
        new_items = (file_info_t *)realloc(vec->items, ncap * sizeof(file_info_t));
        if (new_items == NULL) {
            return -1;
        }
        vec->items = new_items;
        vec->cap = ncap;
    }

    vec->items[vec->len++] = *item;
    return 0;
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
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static int dir_vec_push(dir_vec_t *vec, const char *path, const struct stat *st) {
    dir_operand_t *new_items;
    size_t ncap;

    if (vec->len == vec->cap) {
        ncap = vec->cap == 0 ? 16 : vec->cap * 2;
        new_items = (dir_operand_t *)realloc(vec->items, ncap * sizeof(dir_operand_t));
        if (new_items == NULL) {
            return -1;
        }
        vec->items = new_items;
        vec->cap = ncap;
    }

    vec->items[vec->len].path = strdup(path);
    if (vec->items[vec->len].path == NULL) {
        return -1;
    }
    vec->items[vec->len].st = *st;
    vec->len++;
    return 0;
}

static void set_exit_code(ls_runtime_t *rt, int code) {
    if (code > rt->exit_code) {
        rt->exit_code = code;
    }
}

static void warn_errno(ls_runtime_t *rt, const char *what, const char *path, int err, int serious) {
    fprintf(stderr, "ls: %s '%s': %s\n", what, path, strerror(err));
    set_exit_code(rt, serious ? LS_EXIT_SERIOUS : LS_EXIT_MINOR);
}

static int visit_contains(const ls_runtime_t *rt, dev_t dev, ino_t ino) {
    size_t i;
    for (i = 0; i < rt->len; i++) {
        if (rt->keys[i].dev == dev && rt->keys[i].ino == ino) {
            return 1;
        }
    }
    return 0;
}

static int visit_add(ls_runtime_t *rt, dev_t dev, ino_t ino) {
    visit_key_t *new_keys;
    size_t ncap;

    if (visit_contains(rt, dev, ino)) {
        return 0;
    }

    if (rt->len == rt->cap) {
        ncap = rt->cap == 0 ? 32 : rt->cap * 2;
        new_keys = (visit_key_t *)realloc(rt->keys, ncap * sizeof(visit_key_t));
        if (new_keys == NULL) {
            return -1;
        }
        rt->keys = new_keys;
        rt->cap = ncap;
    }

    rt->keys[rt->len].dev = dev;
    rt->keys[rt->len].ino = ino;
    rt->len++;
    return 0;
}

static char *path_join(const char *dir, const char *name) {
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    size_t need = dl + 1 + nl + 1;
    char *out = (char *)malloc(need);

    if (out == NULL) {
        return NULL;
    }

    if (dl == 1 && dir[0] == '/') {
        snprintf(out, need, "/%s", name);
    } else if (dl > 0 && dir[dl - 1] == '/') {
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
        {
            char *nb = (char *)realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
    }
}

static int match_pattern(const char *pattern, const char *name) {
    return pattern != NULL && fnmatch(pattern, name, 0) == 0;
}

static int should_include_name(const char *name, const ls_config_t *config) {
    if (!config->all) {
        if (name[0] == '.') {
            if (!config->almost_all) {
                return 0;
            }
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                return 0;
            }
        }
    }

    if (match_pattern(config->ignore_pattern, name)) {
        return 0;
    }

    if (config->recursive && match_pattern(config->hide_pattern, name)) {
        return 0;
    }

    return 1;
}

static int collect_path_info(ls_runtime_t *rt,
                             const char *display_name,
                             const char *full_path,
                             bool follow_symlink,
                             bool serious_on_error,
                             size_t index,
                             file_info_t *out) {
    struct stat lst;

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

    if (lstat(full_path, &lst) != 0) {
        warn_errno(rt, "cannot access", full_path, errno, serious_on_error ? 1 : 0);
        file_info_clear(out);
        return 1;
    }

    out->display_as_symlink = S_ISLNK(lst.st_mode);
    if (out->display_as_symlink) {
        out->link_target = readlink_dup(full_path);
    }

    out->st = lst;
    out->stat_ok = true;

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
                warn_errno(rt, "cannot dereference", full_path, errno, 0);
            }
        }
    }

    return 0;
}

static int list_directory(ls_runtime_t *rt,
                          const char *path,
                          const ls_config_t *config,
                          bool command_line_arg,
                          bool print_header);

static int recurse_into_subdirs(ls_runtime_t *rt,
                                const char *parent,
                                file_vec_t *entries,
                                const ls_config_t *config) {
    size_t i;

    for (i = 0; i < entries->len; i++) {
        file_info_t *f = &entries->items[i];

        if (!S_ISDIR(f->st.st_mode)) {
            continue;
        }
        if (strcmp(f->name, ".") == 0 || strcmp(f->name, "..") == 0) {
            continue;
        }

        if (visit_contains(rt, f->st.st_dev, f->st.st_ino)) {
            fprintf(stderr, "ls: skipping directory '%s': filesystem loop detected\n", f->full_path);
            set_exit_code(rt, LS_EXIT_MINOR);
            continue;
        }

        if (visit_add(rt, f->st.st_dev, f->st.st_ino) != 0) {
            return -1;
        }

        (void)parent;
        putchar('\n');
        if (list_directory(rt, f->full_path, config, false, true) != 0) {
            return -1;
        }
    }

    return 0;
}

static int list_directory(ls_runtime_t *rt,
                          const char *path,
                          const ls_config_t *config,
                          bool command_line_arg,
                          bool print_header) {
    DIR *dir;
    struct dirent *ent;
    file_vec_t entries;
    size_t idx = 0;

    memset(&entries, 0, sizeof(entries));

    dir = opendir(path);
    if (dir == NULL) {
        warn_errno(rt, "cannot open directory", path, errno, command_line_arg ? 1 : 0);
        return 0;
    }

    if (print_header) {
        printf("%s:\n", path);
    }

    while ((ent = readdir(dir)) != NULL) {
        file_info_t fi;
        char *full_path;
        int rc;

        if (!should_include_name(ent->d_name, config)) {
            continue;
        }

        full_path = path_join(path, ent->d_name);
        if (full_path == NULL) {
            closedir(dir);
            file_vec_free(&entries);
            return -1;
        }

        rc = collect_path_info(rt,
                               ent->d_name,
                               full_path,
                               config->dereference,
                               false,
                               idx,
                               &fi);
        free(full_path);
        if (rc < 0) {
            closedir(dir);
            file_vec_free(&entries);
            return -1;
        }
        if (rc > 0) {
            continue;
        }

        if (file_vec_push(&entries, &fi) != 0) {
            file_info_clear(&fi);
            closedir(dir);
            file_vec_free(&entries);
            return -1;
        }

        idx++;
    }

    closedir(dir);

    ls_sort_entries(entries.items, entries.len, config);
    ls_print_list(path, entries.items, entries.len, config, true);

    if (config->recursive && !config->directory) {
        if (recurse_into_subdirs(rt, path, &entries, config) != 0) {
            file_vec_free(&entries);
            return -1;
        }
    }

    file_vec_free(&entries);
    return 0;
}

int ls_run(const ls_config_t *config, char **paths, int path_count) {
    ls_runtime_t rt;
    file_vec_t files;
    dir_vec_t dirs;
    size_t i;
    const char *implicit[1] = {"."};

    memset(&rt, 0, sizeof(rt));
    memset(&files, 0, sizeof(files));
    memset(&dirs, 0, sizeof(dirs));

    if (path_count == 0) {
        paths = (char **)implicit;
        path_count = 1;
    }

    for (i = 0; i < (size_t)path_count; i++) {
        file_info_t fi;
        bool follow_arg = config->dereference || config->dereference_args;
        int rc;

        rc = collect_path_info(&rt,
                               paths[i],
                               paths[i],
                               follow_arg,
                               true,
                               i,
                               &fi);
        if (rc < 0) {
            set_exit_code(&rt, LS_EXIT_SERIOUS);
            goto done;
        }
        if (rc > 0) {
            continue;
        }

        if (S_ISDIR(fi.st.st_mode) && !config->directory) {
            if (dir_vec_push(&dirs, paths[i], &fi.st) != 0) {
                file_info_clear(&fi);
                set_exit_code(&rt, LS_EXIT_SERIOUS);
                goto done;
            }
            file_info_clear(&fi);
        } else {
            if (file_vec_push(&files, &fi) != 0) {
                file_info_clear(&fi);
                set_exit_code(&rt, LS_EXIT_SERIOUS);
                goto done;
            }
        }
    }

    if (files.len > 0) {
        ls_sort_entries(files.items, files.len, config);
        ls_print_list(NULL, files.items, files.len, config, false);
    }

    for (i = 0; i < dirs.len; i++) {
        bool print_header = (dirs.len > 1) || (files.len > 0) || config->recursive;

        if (i > 0 || (files.len > 0 && i == 0)) {
            putchar('\n');
        }

        if (visit_add(&rt, dirs.items[i].st.st_dev, dirs.items[i].st.st_ino) != 0) {
            set_exit_code(&rt, LS_EXIT_SERIOUS);
            goto done;
        }

        if (list_directory(&rt, dirs.items[i].path, config, true, print_header) != 0) {
            set_exit_code(&rt, LS_EXIT_SERIOUS);
            goto done;
        }
    }

done:
    free(rt.keys);
    file_vec_free(&files);
    dir_vec_free(&dirs);

    return rt.exit_code;
}
