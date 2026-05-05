#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
usage(void)
{
    fprintf(stderr, "usage: rm [-f] [-rR] file ...\n");
}

static void
report_remove_error(const char *path)
{
    fprintf(stderr, "rm: cannot remove '%s': %s\n", path, strerror(errno));
}

static char *
join_path(const char *dir, const char *name)
{
    size_t dir_len;
    size_t name_len;
    size_t need_sep;
    char *path;

    dir_len = strlen(dir);
    name_len = strlen(name);
    need_sep = (dir_len > 0 && dir[dir_len - 1] != '/') ? 1u : 0u;
    path = malloc(dir_len + need_sep + name_len + 1u);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, dir, dir_len);
    if (need_sep) {
        path[dir_len++] = '/';
    }
    memcpy(path + dir_len, name, name_len + 1u);
    return path;
}

static int remove_path(const char *path, bool recursive, bool force);

static int
remove_directory(const char *path, bool force)
{
    DIR *dir;
    struct dirent *entry;
    int status;

    dir = opendir(path);
    if (dir == NULL) {
        if (force && errno == ENOENT) {
            return 0;
        }
        report_remove_error(path);
        return 1;
    }

    status = 0;
    while ((entry = readdir(dir)) != NULL) {
        char *child;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        child = join_path(path, entry->d_name);
        if (child == NULL) {
            fprintf(stderr, "rm: cannot remove '%s': out of memory\n", path);
            status = 1;
            continue;
        }
        if (remove_path(child, true, force) != 0) {
            status = 1;
        }
        free(child);
    }

    if (closedir(dir) != 0) {
        status = 1;
    }
    if (rmdir(path) != 0) {
        if (!(force && errno == ENOENT)) {
            report_remove_error(path);
            status = 1;
        }
    }
    return status;
}

static int
remove_path(const char *path, bool recursive, bool force)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        if (force && errno == ENOENT) {
            return 0;
        }
        report_remove_error(path);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            errno = EISDIR;
            report_remove_error(path);
            return 1;
        }
        return remove_directory(path, force);
    }

    if (unlink(path) != 0) {
        if (force && errno == ENOENT) {
            return 0;
        }
        report_remove_error(path);
        return 1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    bool force;
    bool recursive;
    int first_path;
    int status;
    int i;

    force = false;
    recursive = false;
    first_path = 1;
    for (i = 1; i < argc; ++i) {
        const char *arg;
        size_t j;

        arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            first_path = i;
            break;
        }
        if (strcmp(arg, "--") == 0) {
            first_path = i + 1;
            break;
        }
        for (j = 1; arg[j] != '\0'; ++j) {
            switch (arg[j]) {
            case 'f':
                force = true;
                break;
            case 'r':
            case 'R':
                recursive = true;
                break;
            default:
                usage();
                return 1;
            }
        }
        first_path = i + 1;
    }

    if (first_path >= argc) {
        if (force) {
            return 0;
        }
        usage();
        return 1;
    }

    status = 0;
    for (i = first_path; i < argc; ++i) {
        if (remove_path(argv[i], recursive, force) != 0) {
            status = 1;
        }
    }
    return status;
}

