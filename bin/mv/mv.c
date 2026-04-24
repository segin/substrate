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
    fprintf(stderr, "usage: mv [-f] source target\n       mv [-f] source ... directory\n");
}

static const char *
base_name(const char *path)
{
    const char *slash;

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }
    while (slash[1] == '\0' && slash > path) {
        size_t len;
        char *tmp;

        len = (size_t)(slash - path);
        tmp = malloc(len + 1u);
        if (tmp == NULL) {
            return path;
        }
        memcpy(tmp, path, len);
        tmp[len] = '\0';
        slash = strrchr(tmp, '/');
        free(tmp);
        if (slash == NULL) {
            return path;
        }
    }
    return slash[1] != '\0' ? slash + 1 : path;
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

static int
move_one(const char *src, const char *dst)
{
    if (rename(src, dst) != 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
        return 1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int first_path;
    int operands;
    int status;
    int i;

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
            if (arg[j] != 'f') {
                usage();
                return 1;
            }
        }
        first_path = i + 1;
    }

    operands = argc - first_path;
    if (operands < 2) {
        usage();
        return 1;
    }

    if (operands == 2) {
        return move_one(argv[first_path], argv[first_path + 1]);
    }

    {
        const char *dst_dir;
        struct stat st;

        dst_dir = argv[argc - 1];
        if (stat(dst_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "mv: target '%s' is not a directory\n", dst_dir);
            return 1;
        }

        status = 0;
        for (i = first_path; i < argc - 1; ++i) {
            char *dst;

            dst = join_path(dst_dir, base_name(argv[i]));
            if (dst == NULL) {
                fprintf(stderr, "mv: cannot move '%s': out of memory\n", argv[i]);
                status = 1;
                continue;
            }
            if (move_one(argv[i], dst) != 0) {
                status = 1;
            }
            free(dst);
        }
    }

    return status;
}

