#include <stdlib.h>
#include <string.h>

#include "mv_path.h"
#include <sys/stat.h>

void mv_path_strip_trailing_slashes(char *path)
{
    size_t len;

    if (path == NULL) {
        return;
    }
    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

const char *mv_path_basename(const char *path)
{
    const char *slash;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

char *mv_path_join(const char *dir, const char *name)
{
    size_t dlen;
    size_t nlen;
    size_t sep;
    char *out;

    if (dir == NULL || name == NULL) {
        return NULL;
    }
    dlen = strlen(dir);
    nlen = strlen(name);
    sep = (dlen > 0 && dir[dlen - 1] != '/') ? 1u : 0u;
    out = (char *)malloc(dlen + sep + nlen + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, dir, dlen);
    if (sep) {
        out[dlen++] = '/';
    }
    memcpy(out + dlen, name, nlen + 1u);
    return out;
}

bool mv_path_same_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;

    if (a == NULL || b == NULL) {
        return false;
    }
    if (lstat(a, &sa) != 0 || lstat(b, &sb) != 0) {
        return false;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
