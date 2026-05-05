#include "rmdir_parents.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

static char *
rmdir_dup_range(const char *text, size_t length)
{
    char *copy;

    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

static char *
rmdir_trim_path(const char *path)
{
    size_t length;

    if (path == NULL || path[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') {
        --length;
    }
    return rmdir_dup_range(path, length);
}

static bool
rmdir_is_forbidden_component(const char *path)
{
    size_t length;
    size_t start;
    size_t component_length;

    length = strlen(path);
    start = length;
    while (start > 0 && path[start - 1] != '/') {
        --start;
    }
    component_length = length - start;
    return (component_length == 1 && path[start] == '.') ||
        (component_length == 2 && path[start] == '.' && path[start + 1] == '.');
}

static int
rmdir_split_path(const char *path, char **parent_out, char **name_out)
{
    const char *last_slash;

    *parent_out = NULL;
    *name_out = NULL;

    last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        *name_out = rmdir_dup_range(path, strlen(path));
        return *name_out == NULL ? -1 : 0;
    }
    if (last_slash == path) {
        *parent_out = rmdir_dup_range("/", 1);
        *name_out = rmdir_dup_range(path + 1, strlen(path + 1));
        return (*parent_out == NULL || *name_out == NULL) ? -1 : 0;
    }

    *parent_out = rmdir_dup_range(path, (size_t)(last_slash - path));
    *name_out = rmdir_dup_range(last_slash + 1, strlen(last_slash + 1));
    return (*parent_out == NULL || *name_out == NULL) ? -1 : 0;
}

static void
rmdir_report_verbose(const struct rmdir_options *opts, const char *path)
{
    if (opts->verbose) {
        printf("%s: removing directory, '%s'\n", opts->progname, path);
    }
}

static void
rmdir_report_errno(const struct rmdir_options *opts, const char *path,
    int errnum)
{
    fprintf(stderr, "%s: cannot remove '%s': %s\n", opts->progname, path,
        strerror(errnum));
}

static void
rmdir_report_message(const struct rmdir_options *opts, const char *message,
    const char *path)
{
    fprintf(stderr, "%s: %s '%s'\n", opts->progname, message, path);
}

static void
rmdir_test_pause_before_unlink(void)
{
    char *endptr;
    const char *delay_text;
    unsigned long delay_us;

    delay_text = getenv("RMDIR_TEST_DELAY_US");
    if (delay_text == NULL || delay_text[0] == '\0') {
        return;
    }

    endptr = NULL;
    delay_us = strtoul(delay_text, &endptr, 10);
    if (endptr == delay_text || *endptr != '\0' || delay_us == 0) {
        return;
    }
    usleep((useconds_t)delay_us);
}

static int
rmdir_remove_single(const struct rmdir_options *opts, const char *path)
{
    char *trimmed;
    char *parent_path;
    char *name;
    int parent_fd;
    int result;

    trimmed = NULL;
    parent_path = NULL;
    name = NULL;
    parent_fd = AT_FDCWD;
    result = RMDIR_RESULT_FAILED;

    trimmed = rmdir_trim_path(path);
    if (trimmed == NULL) {
        rmdir_report_errno(opts, path, errno);
        goto done;
    }
    if (strcmp(trimmed, "/") == 0) {
        rmdir_report_message(opts, "refusing to remove", "/");
        goto done;
    }
    if (rmdir_is_forbidden_component(trimmed)) {
        rmdir_report_message(opts, "refusing to remove '.' or '..'", trimmed);
        goto done;
    }
    if (rmdir_split_path(trimmed, &parent_path, &name) != 0) {
        rmdir_report_errno(opts, trimmed, errno);
        goto done;
    }

    if (parent_path != NULL) {
        parent_fd = openat(AT_FDCWD, parent_path, O_RDONLY | O_DIRECTORY);
        if (parent_fd < 0) {
            rmdir_report_errno(opts, trimmed, errno);
            goto done;
        }
    }

    {
        struct stat target_st;

        if (fstatat(parent_fd, name, &target_st, AT_SYMLINK_NOFOLLOW) != 0) {
            rmdir_report_errno(opts, trimmed, errno);
            goto done;
        }
        if (!S_ISDIR(target_st.st_mode)) {
            rmdir_report_errno(opts, trimmed, ENOTDIR);
            goto done;
        }
    }

    rmdir_test_pause_before_unlink();

    if (unlinkat(parent_fd, name, AT_REMOVEDIR) != 0) {
        if ((errno == ENOTEMPTY || errno == EEXIST) &&
            opts->ignore_fail_on_non_empty) {
            result = RMDIR_RESULT_STOP;
            goto done;
        }
        rmdir_report_errno(opts, trimmed, errno);
        goto done;
    }

    rmdir_report_verbose(opts, trimmed);
    result = RMDIR_RESULT_REMOVED;

done:
    if (parent_fd != AT_FDCWD) {
        (void)close(parent_fd);
    }
    free(trimmed);
    free(parent_path);
    free(name);
    return result;
}

int
rmdir_remove_path(const struct rmdir_options *opts, const char *path)
{
    char *current_path;
    int result;

    current_path = rmdir_trim_path(path);
    if (current_path == NULL) {
        rmdir_report_errno(opts, path, errno);
        return RMDIR_RESULT_FAILED;
    }

    result = rmdir_remove_single(opts, current_path);
    if (result != RMDIR_RESULT_REMOVED || !opts->parents) {
        free(current_path);
        return result == RMDIR_RESULT_STOP ? RMDIR_RESULT_REMOVED : result;
    }

    while (strchr(current_path, '/') != NULL) {
        char *slash;

        slash = strrchr(current_path, '/');
        if (slash == current_path) {
            break;
        }
        *slash = '\0';
        if (strcmp(current_path, ".") == 0 || strcmp(current_path, "..") == 0 ||
            current_path[0] == '\0') {
            break;
        }
        if (current_path[0] == '/' && strrchr(current_path, '/') == current_path) {
            break;
        }

        result = rmdir_remove_single(opts, current_path);
        if (result != RMDIR_RESULT_REMOVED) {
            free(current_path);
            return result == RMDIR_RESULT_STOP ? RMDIR_RESULT_REMOVED : result;
        }
    }

    free(current_path);
    return RMDIR_RESULT_REMOVED;
}