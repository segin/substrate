#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mkdir_parents.h"
#include <sys/stat.h>
#include <sys/types.h>

static char *
dup_range(const char *src, size_t len)
{
    char *out;

    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(out, src, len);
    }
    out[len] = '\0';
    return out;
}

static char *
dup_string(const char *src)
{
    return dup_range(src, strlen(src));
}

static char *
append_path(const char *base, const char *component)
{
    size_t base_len;
    size_t comp_len;
    size_t need_slash;
    char *out;

    base_len = strlen(base);
    comp_len = strlen(component);
    need_slash = (base_len > 0 && strcmp(base, "/") != 0) ? 1u : 0u;
    out = (char *)malloc(base_len + need_slash + comp_len + 1);
    if (out == NULL) {
        return NULL;
    }

    if (base_len == 0) {
        memcpy(out, component, comp_len);
        out[comp_len] = '\0';
        return out;
    }

    memcpy(out, base, base_len);
    if (strcmp(base, "/") == 0) {
        memcpy(out + base_len, component, comp_len);
        out[base_len + comp_len] = '\0';
        return out;
    }

    out[base_len] = '/';
    memcpy(out + base_len + 1, component, comp_len);
    out[base_len + 1 + comp_len] = '\0';
    return out;
}

static int
set_failure(char **error_path, int *error_errno, const char *path, int errnum)
{
    if (error_errno != NULL) {
        *error_errno = errnum;
    }
    if (error_path != NULL) {
        *error_path = dup_string(path != NULL ? path : "");
    }
    return -1;
}

static void
close_tracked_fd(int *fdp)
{
    if (fdp != NULL && *fdp >= 0 && *fdp != AT_FDCWD) {
        (void)close(*fdp);
    }
    if (fdp != NULL) {
        *fdp = AT_FDCWD;
    }
}

static int
open_dir_at(int dirfd, const char *name)
{
    int fd;

    do {
        fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

static int
mkdir_at_retry(int dirfd, const char *name, mode_t mode)
{
    int rc;

    do {
        rc = mkdirat(dirfd, name, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
fstatat_retry(int dirfd, const char *name, struct stat *st)
{
    int rc;

    do {
        rc = fstatat(dirfd, name, st, 0);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
fchmod_retry(int fd, mode_t mode)
{
    int rc;

    do {
        rc = fchmod(fd, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

int
mkdir_create_parents(const struct mkdir_options *opts, const char *path,
    mode_t create_mode, bool apply_final_mode, mode_t final_mode,
    char **error_path, int *error_errno)
{
    int dirfd = AT_FDCWD;
    int rootfd = AT_FDCWD;
    char *display = NULL;
    size_t index = 0;
    size_t len;

    if (error_path != NULL) {
        *error_path = NULL;
    }
    if (error_errno != NULL) {
        *error_errno = 0;
    }

    len = strlen(path);
    if (len == 0) {
        return set_failure(error_path, error_errno, path, ENOENT);
    }

    if (path[0] == '/') {
        do {
            rootfd = open("/", O_RDONLY | O_DIRECTORY);
        } while (rootfd < 0 && errno == EINTR);
        if (rootfd < 0) {
            return set_failure(error_path, error_errno, "/", errno);
        }
        dirfd = rootfd;
        display = dup_string("/");
        if (display == NULL) {
            close_tracked_fd(&rootfd);
            return set_failure(error_path, error_errno, "/", ENOMEM);
        }
        while (path[index] == '/') {
            ++index;
        }
        if (path[index] == '\0') {
            free(display);
            close_tracked_fd(&rootfd);
            return 0;
        }
    } else {
        display = dup_string("");
        if (display == NULL) {
            return set_failure(error_path, error_errno, path, ENOMEM);
        }
    }

    while (path[index] != '\0') {
        size_t start;
        size_t end;
        size_t next;
        bool is_last;
        char *component;
        char *candidate;
        int nextfd = AT_FDCWD;
        struct stat st;
        int created = 0;

        while (path[index] == '/') {
            ++index;
        }
        if (path[index] == '\0') {
            break;
        }

        start = index;
        while (path[index] != '\0' && path[index] != '/') {
            ++index;
        }
        end = index;
        next = index;
        while (path[next] == '/') {
            ++next;
        }
        is_last = path[next] == '\0';

        component = dup_range(path + start, end - start);
        if (component == NULL) {
            free(display);
            close_tracked_fd(&rootfd);
            return set_failure(error_path, error_errno, path, ENOMEM);
        }

        candidate = append_path(display, component);
        if (candidate == NULL) {
            free(component);
            free(display);
            close_tracked_fd(&rootfd);
            return set_failure(error_path, error_errno, path, ENOMEM);
        }

        if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
            if (fstatat_retry(dirfd, component, &st) != 0) {
                int saved_errno = errno;

                free(component);
                free(candidate);
                free(display);
                close_tracked_fd(&rootfd);
                return set_failure(error_path, error_errno, path,
                    saved_errno);
            }
            if (!S_ISDIR(st.st_mode)) {
                free(component);
                free(candidate);
                free(display);
                close_tracked_fd(&rootfd);
                return set_failure(error_path, error_errno, path, ENOTDIR);
            }
            if (!is_last) {
                nextfd = open_dir_at(dirfd, component);
                if (nextfd < 0) {
                    int saved_errno = errno;
                    int rc;

                    free(component);
                    free(display);
                    close_tracked_fd(&rootfd);
                    rc = set_failure(error_path, error_errno, candidate,
                        saved_errno);
                    free(candidate);
                    return rc;
                }
            }
        } else {
            mode_t step_mode = is_last ? create_mode : 0777;

            if (mkdir_at_retry(dirfd, component, step_mode) == 0) {
                created = 1;
                if (opts->verbose) {
                    printf("%s: created directory '%s'\n", opts->progname,
                        candidate);
                }
            } else if (errno == EEXIST) {
                if (fstatat_retry(dirfd, component, &st) != 0) {
                    int saved_errno = errno;
                    int rc;

                    free(component);
                    free(display);
                    close_tracked_fd(&rootfd);
                    rc = set_failure(error_path, error_errno, candidate,
                        saved_errno);
                    free(candidate);
                    return rc;
                }
                if (!S_ISDIR(st.st_mode)) {
                    int rc;

                    free(component);
                    free(display);
                    close_tracked_fd(&rootfd);
                    rc = set_failure(error_path, error_errno, candidate,
                        ENOTDIR);
                    free(candidate);
                    return rc;
                }
            } else {
                int saved_errno = errno;
                int rc;

                free(component);
                free(display);
                close_tracked_fd(&rootfd);
                rc = set_failure(error_path, error_errno, candidate,
                    saved_errno);
                free(candidate);
                return rc;
            }

            if (!is_last || (is_last && apply_final_mode)) {
                nextfd = open_dir_at(dirfd, component);
                if (nextfd < 0) {
                    int saved_errno = errno;
                    int rc;

                    free(component);
                    free(display);
                    close_tracked_fd(&rootfd);
                    rc = set_failure(error_path, error_errno, candidate,
                        saved_errno);
                    free(candidate);
                    return rc;
                }
            }

            if (is_last && created && apply_final_mode) {
                if (fchmod_retry(nextfd, final_mode) != 0) {
                    int saved_errno = errno;
                    int rc;

                    free(component);
                    free(display);
                    close_tracked_fd(&nextfd);
                    close_tracked_fd(&rootfd);
                    rc = set_failure(error_path, error_errno, candidate,
                        saved_errno);
                    free(candidate);
                    return rc;
                }
            }
        }

        free(display);
        display = candidate;
        free(component);

        if (!is_last) {
            if (dirfd != rootfd) {
                close_tracked_fd(&dirfd);
            }
            dirfd = nextfd;
            nextfd = AT_FDCWD;
        } else {
            close_tracked_fd(&nextfd);
        }

        index = next;
    }

    free(display);
    if (dirfd != rootfd) {
        close_tracked_fd(&dirfd);
    }
    close_tracked_fd(&rootfd);
    return 0;
}