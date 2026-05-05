#include "ln.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

char *
ln_strdup(const char *s)
{
    size_t len;
    char *out;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, s, len + 1);
    return out;
}

const char *
ln_basename_const(const char *path)
{
    size_t len;
    const char *base;

    if (!path || *path == '\0') {
        return path ? path : "";
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    if (len == 1 && path[0] == '/') {
        return path;
    }

    base = path + len;
    while (base > path && base[-1] != '/') {
        --base;
    }

    return base;
}

char *
ln_dirname_copy(const char *path)
{
    size_t len;
    size_t i;
    char *out;

    if (!path || *path == '\0') {
        return ln_strdup(".");
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    if (len == 1 && path[0] == '/') {
        return ln_strdup("/");
    }

    i = len;
    while (i > 0 && path[i - 1] != '/') {
        --i;
    }

    if (i == 0) {
        return ln_strdup(".");
    }

    while (i > 1 && path[i - 1] == '/') {
        --i;
    }

    out = (char *)malloc(i + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, path, i);
    out[i] = '\0';
    return out;
}

char *
ln_path_join(const char *left, const char *right)
{
    size_t left_len;
    size_t right_len;
    size_t need_slash;
    char *out;

    if (!left || !right) {
        errno = EINVAL;
        return NULL;
    }

    left_len = strlen(left);
    right_len = strlen(right);
    need_slash = (left_len > 0 && left[left_len - 1] != '/') ? 1U : 0U;

    out = (char *)malloc(left_len + need_slash + right_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, left, left_len);
    if (need_slash) {
        out[left_len] = '/';
    }
    memcpy(out + left_len + need_slash, right, right_len);
    out[left_len + need_slash + right_len] = '\0';
    return out;
}

static int
ln_append_text(char **buf, size_t *cap, size_t *len, const char *text)
{
    size_t text_len;
    char *grown;

    if (!buf || !cap || !len || !text) {
        errno = EINVAL;
        return -1;
    }

    text_len = strlen(text);
    if (*len + text_len + 1 > *cap) {
        size_t next_cap;

        next_cap = *cap;
        while (*len + text_len + 1 > next_cap) {
            next_cap *= 2;
        }

        grown = (char *)realloc(*buf, next_cap);
        if (!grown) {
            return -1;
        }
        *buf = grown;
        *cap = next_cap;
    }

    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = '\0';
    return 0;
}

static char *
ln_build_normalized_path(char **segments, size_t segment_count, bool absolute, size_t original_len)
{
    size_t out_cap;
    char *out;
    size_t pos;
    size_t i;

    out_cap = original_len + 4;
    out = (char *)malloc(out_cap);
    if (!out) {
        return NULL;
    }

    pos = 0;
    if (segment_count == 0) {
        out[pos++] = absolute ? '/' : '.';
        out[pos] = '\0';
        return out;
    }

    if (absolute) {
        out[pos++] = '/';
    }

    for (i = 0; i < segment_count; ++i) {
        size_t segment_len;

        segment_len = strlen(segments[i]);
        if (i > 0) {
            out[pos++] = '/';
        }
        memcpy(out + pos, segments[i], segment_len);
        pos += segment_len;
    }

    out[pos] = '\0';
    return out;
}

char *
ln_path_normalize_copy(const char *path)
{
    bool absolute;
    size_t len;
    char *tmp;
    char *saveptr;
    char *tok;
    char **segments;
    size_t segment_cap;
    size_t segment_count;
    char *out;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (*path == '\0') {
        return ln_strdup(".");
    }

    len = strlen(path);
    absolute = (path[0] == '/');

    tmp = ln_strdup(path);
    if (!tmp) {
        return NULL;
    }

    segment_cap = len + 1;
    segments = (char **)calloc(segment_cap, sizeof(char *));
    if (!segments) {
        free(tmp);
        return NULL;
    }

    segment_count = 0;
    saveptr = NULL;
    tok = strtok_r(tmp, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            tok = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        if (strcmp(tok, "..") == 0) {
            if (segment_count > 0 && strcmp(segments[segment_count - 1], "..") != 0) {
                --segment_count;
            } else if (!absolute) {
                segments[segment_count++] = tok;
            }
            tok = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        segments[segment_count++] = tok;
        tok = strtok_r(NULL, "/", &saveptr);
    }

    out = ln_build_normalized_path(segments, segment_count, absolute, len);
    free(segments);
    free(tmp);
    return out;
}

char *
ln_path_absolute_normalized(const char *path)
{
    char cwd[PATH_MAX];
    char *joined;
    char *normalized;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (path[0] == '/') {
        return ln_path_normalize_copy(path);
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        return NULL;
    }

    joined = ln_path_join(cwd, path);
    if (!joined) {
        return NULL;
    }

    normalized = ln_path_normalize_copy(joined);
    free(joined);
    return normalized;
}

static void
ln_components_free(char **parts, size_t count)
{
    size_t i;

    if (!parts) {
        return;
    }

    for (i = 0; i < count; ++i) {
        free(parts[i]);
    }
    free(parts);
}

static int
ln_split_abs_components(const char *abs_path, char ***out_parts, size_t *out_count)
{
    char *tmp;
    char *saveptr;
    char *tok;
    char **parts;
    size_t cap;
    size_t count;

    if (!abs_path || abs_path[0] != '/' || !out_parts || !out_count) {
        errno = EINVAL;
        return -1;
    }

    tmp = ln_strdup(abs_path + 1);
    if (!tmp) {
        return -1;
    }

    cap = strlen(abs_path) + 1;
    parts = (char **)calloc(cap, sizeof(char *));
    if (!parts) {
        free(tmp);
        return -1;
    }

    count = 0;
    saveptr = NULL;
    tok = strtok_r(tmp, "/", &saveptr);
    while (tok) {
        parts[count] = ln_strdup(tok);
        if (!parts[count]) {
            ln_components_free(parts, count);
            free(tmp);
            return -1;
        }
        ++count;
        tok = strtok_r(NULL, "/", &saveptr);
    }

    free(tmp);
    *out_parts = parts;
    *out_count = count;
    return 0;
}

static char *
ln_build_relative_path(char **target_parts, size_t target_count,
                       size_t base_count, size_t common_len)
{
    size_t cap;
    size_t len;
    char *out;
    size_t i;

    cap = 64;
    len = 0;
    out = (char *)malloc(cap);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (i = common_len; i < base_count; ++i) {
        if (ln_append_text(&out, &cap, &len, "../") != 0) {
            free(out);
            return NULL;
        }
    }

    for (i = common_len; i < target_count; ++i) {
        if (ln_append_text(&out, &cap, &len, target_parts[i]) != 0) {
            free(out);
            return NULL;
        }
        if (i + 1 < target_count && ln_append_text(&out, &cap, &len, "/") != 0) {
            free(out);
            return NULL;
        }
    }

    if (len == 0 && ln_append_text(&out, &cap, &len, ".") != 0) {
        free(out);
        return NULL;
    }

    if (len >= 3 && strcmp(out + len - 3, "../") == 0) {
        out[len - 1] = '\0';
    }

    return out;
}

char *
ln_relative_from_to(const char *target_abs, const char *base_abs)
{
    char **target_parts;
    char **base_parts;
    size_t target_count;
    size_t base_count;
    size_t common_len;
    char *relative;

    if (!target_abs || !base_abs || target_abs[0] != '/' || base_abs[0] != '/') {
        errno = EINVAL;
        return NULL;
    }

    if (ln_split_abs_components(target_abs, &target_parts, &target_count) != 0) {
        return NULL;
    }
    if (ln_split_abs_components(base_abs, &base_parts, &base_count) != 0) {
        ln_components_free(target_parts, target_count);
        return NULL;
    }

    common_len = 0;
    while (common_len < target_count && common_len < base_count &&
           strcmp(target_parts[common_len], base_parts[common_len]) == 0) {
        ++common_len;
    }

    relative = ln_build_relative_path(target_parts, target_count, base_count, common_len);
    ln_components_free(target_parts, target_count);
    ln_components_free(base_parts, base_count);
    return relative;
}

bool
ln_same_entry(const char *left, const char *right)
{
    struct stat left_st;
    struct stat right_st;

    if (!left || !right) {
        return false;
    }

    if (lstat(left, &left_st) != 0 || lstat(right, &right_st) != 0) {
        return false;
    }

    return left_st.st_dev == right_st.st_dev && left_st.st_ino == right_st.st_ino;
}

bool
ln_exists_lstat(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

static int
ln_stat_target_operand(const char *path, bool no_follow, struct stat *out)
{
    if (no_follow) {
        return lstat(path, out);
    }
    return stat(path, out);
}

bool
ln_is_existing_dir_operand(const char *path, bool no_follow)
{
    struct stat st;

    if (ln_stat_target_operand(path, no_follow, &st) != 0) {
        return false;
    }

    return S_ISDIR(st.st_mode);
}
