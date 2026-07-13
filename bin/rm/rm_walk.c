#include "rm_walk.h"

#include "rm_safety.h"
#include "rm_scrub.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
rm_report_errno(const struct rm_options *opts, const char *path, int errnum)
{
    if (!opts->force) {
        fprintf(stderr, "%s: cannot remove '%s': %s\n", opts->progname, path,
            strerror(errnum));
    }
}

static void
rm_report_message(const struct rm_options *opts, const char *message,
    const char *path)
{
    fprintf(stderr, "%s: %s '%s'\n", opts->progname, message, path);
}

static void
rm_report_boundary_skip(const struct rm_options *opts, const char *path)
{
    if (!opts->force) {
        fprintf(stderr,
            "%s: skipping '%s': crosses filesystem boundary\n",
            opts->progname, path);
    }
}

static void
rm_report_verbose_removal(const struct rm_options *opts, const char *path,
    bool directory)
{
    if (opts->verbose) {
        if (directory) {
            printf("%s: removed directory '%s'\n", opts->progname, path);
        } else {
            printf("%s: removed '%s'\n", opts->progname, path);
        }
    }
}

static char *
rm_join_display_path(const char *base, const char *name)
{
    size_t base_len;
    size_t name_len;
    size_t need_separator;
    char *joined;

    base_len = strlen(base);
    name_len = strlen(name);
    need_separator = (strcmp(base, "/") == 0 || base_len == 0 ||
        base[base_len - 1] == '/') ? 0u : 1u;
    joined = (char *)malloc(base_len + need_separator + name_len + 1);
    if (joined == NULL) {
        return NULL;
    }
    memcpy(joined, base, base_len);
    if (need_separator != 0) {
        joined[base_len++] = '/';
    }
    memcpy(joined + base_len, name, name_len + 1);
    return joined;
}

static int
rm_stat_parent(int parent_fd, struct stat *parent_st)
{
    if (parent_fd == AT_FDCWD) {
        return stat(".", parent_st);
    }
    return fstat(parent_fd, parent_st);
}

static int
rm_should_remove(struct rm_walk_state *state, const struct stat *target_st,
    int parent_fd, const char *path)
{
    struct stat parent_st;
    bool write_protected;

    if (state->interrupted != NULL && *state->interrupted != 0) {
        errno = EINTR;
        return -1;
    }
    if (state->opts->force) {
        return 1;
    }

    write_protected = false;
    if (rm_stat_parent(parent_fd, &parent_st) == 0) {
        write_protected = rm_target_is_write_protected(target_st, &parent_st);
    }

    if (state->opts->prompt_mode != RM_PROMPT_ALWAYS && !write_protected) {
        return 1;
    }

    if (state->prompt_input == NULL) {
        state->prompt_input = rm_open_prompt_stream();
    }
    if (state->prompt_input == NULL) {
        return 0;
    }
    return rm_prompt_removal(state->prompt_input,
        write_protected && state->opts->prompt_mode != RM_PROMPT_ALWAYS,
        rm_file_type_name(target_st->st_mode), path);
}

static int rm_remove_at(struct rm_walk_state *state, int parent_fd,
    const char *name, const char *display_path, bool had_trailing_slash,
    dev_t boundary_dev, bool top_level);

static int
rm_remove_directory(struct rm_walk_state *state, int parent_fd,
    const char *name, const char *display_path, const struct stat *directory_st,
    dev_t boundary_dev)
{
    DIR *directory_stream;
    int directory_fd;
    int result;

    /* Bound recursion so a very deep tree can't exhaust the C stack or
     * (with one held-open fd per level) RLIMIT_NOFILE mid-delete (RM-01). */
    if (state->depth >= RM_MAX_DEPTH) {
        rm_report_errno(state->opts, display_path, ELOOP);
        return RM_WALK_FAILED;
    }

    if (state->opts->one_file_system && directory_st->st_dev != boundary_dev) {
        rm_report_boundary_skip(state->opts, display_path);
        return RM_WALK_SKIPPED;
    }

    result = rm_should_remove(state, directory_st, parent_fd, display_path);
    if (result <= 0) {
        return result < 0 ? RM_WALK_FAILED : RM_WALK_SKIPPED;
    }

    directory_fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY |
        O_NOFOLLOW);
    if (directory_fd < 0) {
        rm_report_errno(state->opts, display_path, errno);
        return RM_WALK_FAILED;
    }

    directory_stream = fdopendir(directory_fd);
    if (directory_stream == NULL) {
        int saved_errno;

        saved_errno = errno;
        (void)close(directory_fd);
        rm_report_errno(state->opts, display_path, saved_errno);
        return RM_WALK_FAILED;
    }

    /*
     * Re-verify the file-system boundary against the actually-opened fd, not
     * the pre-openat fstatat: a rename swap between the two could otherwise
     * let --one-file-system descend across the boundary (RM-03).
     */
    if (state->opts->one_file_system) {
        struct stat opened_st;
        if (fstat(directory_fd, &opened_st) != 0) {
            rm_report_errno(state->opts, display_path, errno);
            (void)closedir(directory_stream);
            return RM_WALK_FAILED;
        }
        if (opened_st.st_dev != boundary_dev) {
            rm_report_boundary_skip(state->opts, display_path);
            (void)closedir(directory_stream);
            return RM_WALK_SKIPPED;
        }
    }

    state->depth++;
    result = RM_WALK_REMOVED;
    for (;;) {
        struct dirent *entry;

        errno = 0;
        entry = readdir(directory_stream);
        if (entry == NULL) {
            if (errno != 0) {
                rm_report_errno(state->opts, display_path, errno);
                result = RM_WALK_FAILED;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        {
            char *child_display;
            int child_result;

            child_display = rm_join_display_path(display_path, entry->d_name);
            if (child_display == NULL) {
                rm_report_errno(state->opts, display_path, ENOMEM);
                result = RM_WALK_FAILED;
                break;
            }

            child_result = rm_remove_at(state, dirfd(directory_stream),
                entry->d_name, child_display, false, boundary_dev, false);
            free(child_display);

            if (child_result == RM_WALK_FAILED) {
                /* Record the failure but keep removing the remaining
                 * siblings rather than abandoning them (RM-02). The
                 * non-empty parent will then fail its own rmdir below. */
                result = RM_WALK_FAILED;
                continue;
            }
            if (child_result == RM_WALK_SKIPPED && result != RM_WALK_FAILED) {
                result = RM_WALK_SKIPPED;
            }
        }
    }

    state->depth--;
    if (closedir(directory_stream) != 0 && result != RM_WALK_FAILED) {
        rm_report_errno(state->opts, display_path, errno);
        result = RM_WALK_FAILED;
    }
    if (result != RM_WALK_REMOVED) {
        return result;
    }

    if (unlinkat(parent_fd, name, AT_REMOVEDIR) != 0) {
        rm_report_errno(state->opts, display_path, errno);
        return RM_WALK_FAILED;
    }

    rm_report_verbose_removal(state->opts, display_path, true);
    return RM_WALK_REMOVED;
}

static int
rm_remove_root(struct rm_walk_state *state)
{
    DIR *root_stream;
    int root_fd;
    int result;
    struct stat root_st;

    if (lstat("/", &root_st) != 0) {
        rm_report_errno(state->opts, "/", errno);
        return RM_WALK_FAILED;
    }

    result = rm_should_remove(state, &root_st, AT_FDCWD, "/");
    if (result <= 0) {
        return result < 0 ? RM_WALK_FAILED : RM_WALK_SKIPPED;
    }

    root_fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root_fd < 0) {
        rm_report_errno(state->opts, "/", errno);
        return RM_WALK_FAILED;
    }

    root_stream = fdopendir(root_fd);
    if (root_stream == NULL) {
        int saved_errno;

        saved_errno = errno;
        (void)close(root_fd);
        rm_report_errno(state->opts, "/", saved_errno);
        return RM_WALK_FAILED;
    }

    result = RM_WALK_REMOVED;
    for (;;) {
        struct dirent *entry;

        errno = 0;
        entry = readdir(root_stream);
        if (entry == NULL) {
            if (errno != 0) {
                rm_report_errno(state->opts, "/", errno);
                result = RM_WALK_FAILED;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        {
            char *child_display;
            int child_result;

            child_display = rm_join_display_path("/", entry->d_name);
            if (child_display == NULL) {
                rm_report_errno(state->opts, "/", ENOMEM);
                result = RM_WALK_FAILED;
                break;
            }
            child_result = rm_remove_at(state, dirfd(root_stream),
                entry->d_name, child_display, false, root_st.st_dev, false);
            free(child_display);

            if (child_result == RM_WALK_FAILED) {
                /* Record the failure but keep removing the remaining
                 * siblings rather than abandoning them (RM-02). The
                 * non-empty parent will then fail its own rmdir below. */
                result = RM_WALK_FAILED;
                continue;
            }
            if (child_result == RM_WALK_SKIPPED && result != RM_WALK_FAILED) {
                result = RM_WALK_SKIPPED;
            }
        }
    }

    (void)closedir(root_stream);
    if (result == RM_WALK_REMOVED) {
        rm_report_errno(state->opts, "/", EBUSY);
        return RM_WALK_FAILED;
    }
    return result;
}

static int
rm_remove_at(struct rm_walk_state *state, int parent_fd, const char *name,
    const char *display_path, bool had_trailing_slash, dev_t boundary_dev,
    bool top_level)
{
    struct stat target_st;
    int prompt_result;

    if (fstatat(parent_fd, name, &target_st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (state->opts->force && errno == ENOENT) {
            return RM_WALK_REMOVED;
        }
        rm_report_errno(state->opts, display_path, errno);
        return RM_WALK_FAILED;
    }

    if (had_trailing_slash && !S_ISDIR(target_st.st_mode)) {
        rm_report_errno(state->opts, display_path, ENOTDIR);
        return RM_WALK_FAILED;
    }

    if (S_ISDIR(target_st.st_mode)) {
        if (state->opts->recursive) {
            dev_t next_boundary;

            next_boundary = top_level ? target_st.st_dev : boundary_dev;
            return rm_remove_directory(state, parent_fd, name, display_path,
                &target_st, next_boundary);
        }
        if (!state->opts->dir_mode) {
            rm_report_errno(state->opts, display_path, EISDIR);
            return RM_WALK_FAILED;
        }

        prompt_result = rm_should_remove(state, &target_st, parent_fd,
            display_path);
        if (prompt_result <= 0) {
            return prompt_result < 0 ? RM_WALK_FAILED : RM_WALK_SKIPPED;
        }
        if (unlinkat(parent_fd, name, AT_REMOVEDIR) != 0) {
            rm_report_errno(state->opts, display_path, errno);
            return RM_WALK_FAILED;
        }
        rm_report_verbose_removal(state->opts, display_path, true);
        return RM_WALK_REMOVED;
    }

    prompt_result = rm_should_remove(state, &target_st, parent_fd,
        display_path);
    if (prompt_result <= 0) {
        return prompt_result < 0 ? RM_WALK_FAILED : RM_WALK_SKIPPED;
    }

    /* BSD -P : overwrite regular files before unlinking.  Non-regular
     * files (sockets, FIFOs, device nodes) are unlinked without
     * scrubbing — there's nothing to overwrite. */
    if (state->opts->scrub && S_ISREG(target_st.st_mode) &&
        target_st.st_size > 0) {
        int wfd = openat(parent_fd, name, O_WRONLY | O_NOFOLLOW);
        if (wfd >= 0) {
            if (rm_scrub_file(wfd, target_st.st_size) != 0) {
                rm_report_errno(state->opts, display_path, errno);
                /* FreeBSD: continue with unlink even on scrub failure */
            }
            close(wfd);
        }
    }

    if (unlinkat(parent_fd, name, 0) != 0) {
        if (state->opts->force && errno == ENOENT) {
            return RM_WALK_REMOVED;
        }
        rm_report_errno(state->opts, display_path, errno);
        return RM_WALK_FAILED;
    }

    rm_report_verbose_removal(state->opts, display_path, false);
    return RM_WALK_REMOVED;
}

int
rm_remove_operand(struct rm_walk_state *state, const char *path)
{
    char *display_path;
    char *home_normalized;
    char *name;
    char *normalized;
    char *parent_path;
    bool had_trailing_slash;
    int parent_fd;
    int result;

    display_path = NULL;
    home_normalized = NULL;
    name = NULL;
    normalized = NULL;
    parent_path = NULL;
    had_trailing_slash = false;
    parent_fd = AT_FDCWD;

    if (state->interrupted != NULL && *state->interrupted != 0) {
        rm_report_errno(state->opts, path, EINTR);
        return RM_WALK_FAILED;
    }
    if (rm_operand_is_dot_or_dotdot(path)) {
        rm_report_message(state->opts, "refusing to remove '.' or '..'", path);
        return RM_WALK_FAILED;
    }
    if (rm_split_path(path, &parent_path, &name, &display_path,
            &had_trailing_slash) != 0) {
        rm_report_errno(state->opts, path, errno);
        return RM_WALK_FAILED;
    }

    normalized = rm_normalize_path(path);
    if (normalized != NULL && state->opts->recursive) {
        if (state->opts->preserve_root && strcmp(normalized, "/") == 0) {
            fprintf(stderr,
                "%s: it is dangerous to operate recursively on '/'\n",
                state->opts->progname);
            result = RM_WALK_FAILED;
            goto done;
        }

        if (!state->opts->force) {
            const char *home;

            home = getenv("HOME");
            if (home != NULL && home[0] != '\0') {
                home_normalized = rm_normalize_path(home);
                if (home_normalized != NULL && strcmp(normalized,
                        home_normalized) == 0) {
                    fprintf(stderr,
                        "%s: warning: recursively removing home directory '%s'\n",
                        state->opts->progname, display_path);
                }
            }
        }
    }

    if (strcmp(name, "/") == 0) {
        if (!state->opts->recursive) {
            rm_report_errno(state->opts, display_path, EISDIR);
            result = RM_WALK_FAILED;
            goto done;
        }
        result = rm_remove_root(state);
        goto done;
    }

    if (parent_path != NULL) {
        parent_fd = open(parent_path, O_RDONLY | O_DIRECTORY);
        if (parent_fd < 0) {
            if (state->opts->force && errno == ENOENT) {
                result = RM_WALK_REMOVED;
            } else {
                rm_report_errno(state->opts, display_path, errno);
                result = RM_WALK_FAILED;
            }
            goto done;
        }
    }

    if (state->opts->preserve_root && state->opts->recursive &&
        normalized != NULL && strcmp(normalized, "/") != 0) {
        struct stat target_st;
        struct stat parent_st;

        if (fstatat(parent_fd, name, &target_st, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISDIR(target_st.st_mode) && rm_stat_parent(parent_fd, &parent_st) == 0 &&
            parent_st.st_dev != target_st.st_dev) {
            fprintf(stderr,
                "%s: refusing to remove filesystem root '%s'\n",
                state->opts->progname, display_path);
            result = RM_WALK_FAILED;
            goto done;
        }
    }

    result = rm_remove_at(state, parent_fd, name, display_path,
        had_trailing_slash, 0, true);

done:
    if (parent_fd != AT_FDCWD) {
        (void)close(parent_fd);
    }
    free(parent_path);
    free(name);
    free(display_path);
    free(normalized);
    free(home_normalized);
    return result;
}