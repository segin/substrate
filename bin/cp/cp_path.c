#include "cp_path.h"

#include <stdlib.h>
#include <string.h>

int cp_path_is_dot_or_dotdot(const char *name)
{
    if (!name) {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    return 0;
}

const char *cp_path_basename(const char *path)
{
    const char *base;
    size_t len;

    if (!path || *path == '\0') {
        return path;
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    base = path + len;
    while (base > path && base[-1] != '/') {
        --base;
    }

    return base;
}

char *cp_path_dirname(const char *path)
{
    const char *base;
    size_t dlen;
    char *dir;

    if (!path || *path == '\0') {
        dir = strdup(".");
        return dir;
    }

    base = cp_path_basename(path);
    if (base == path) {
        return strdup(".");
    }

    dlen = (size_t)(base - path);
    while (dlen > 1 && path[dlen - 1] == '/') {
        --dlen;
    }

    dir = (char *)malloc(dlen + 1);
    if (!dir) {
        return NULL;
    }

    memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    return dir;
}

char *cp_path_join(const char *left, const char *right)
{
    size_t l_len;
    size_t r_len;
    size_t need_slash;
    char *out;

    if (!left || !right) {
        return NULL;
    }

    l_len = strlen(left);
    r_len = strlen(right);

    need_slash = (l_len > 0 && left[l_len - 1] != '/') ? 1 : 0;

    out = (char *)malloc(l_len + need_slash + r_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, left, l_len);
    if (need_slash) {
        out[l_len] = '/';
    }
    memcpy(out + l_len + need_slash, right, r_len);
    out[l_len + need_slash + r_len] = '\0';

    return out;
}
