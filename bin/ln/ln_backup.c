#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ln.h"

static ln_backup_mode_t
ln_backup_mode_from_string(const char *method, bool *ok)
{
    if (!method || *method == '\0') {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_EXISTING;
    }

    if (strcmp(method, "none") == 0 || strcmp(method, "off") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_NONE;
    }

    if (strcmp(method, "simple") == 0 || strcmp(method, "never") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_SIMPLE;
    }

    if (strcmp(method, "numbered") == 0 || strcmp(method, "t") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_NUMBERED;
    }

    if (strcmp(method, "existing") == 0 || strcmp(method, "nil") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_EXISTING;
    }

    if (ok) {
        *ok = false;
    }
    return LN_BACKUP_NONE;
}

int
ln_apply_backup_option(ln_options_t *opts, const char *optval)
{
    bool ok;
    const char *version_control;

    if (optval && *optval != '\0') {
        opts->backup_mode = ln_backup_mode_from_string(optval, &ok);
        if (!ok) {
            ln_diag(opts, "invalid backup method '%s'", optval);
            return -1;
        }
        return 0;
    }

    version_control = getenv("VERSION_CONTROL");
    opts->backup_mode = ln_backup_mode_from_string(version_control, &ok);
    if (!ok) {
        opts->backup_mode = LN_BACKUP_EXISTING;
    }
    return 0;
}

static char *
ln_backup_simple_path(const char *dst, const char *suffix)
{
    size_t dst_len;
    size_t suffix_len;
    char *out;

    dst_len = strlen(dst);
    suffix_len = strlen(suffix);
    out = (char *)malloc(dst_len + suffix_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, dst, dst_len);
    memcpy(out + dst_len, suffix, suffix_len);
    out[dst_len + suffix_len] = '\0';
    return out;
}

static char *
ln_backup_numbered_path(const char *dst, unsigned n)
{
    int num_len;
    size_t dst_len;
    char *out;

    dst_len = strlen(dst);
    num_len = snprintf(NULL, 0, ".~%u~", n);
    if (num_len < 0) {
        return NULL;
    }

    out = (char *)malloc(dst_len + (size_t)num_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, dst, dst_len);
    snprintf(out + dst_len, (size_t)num_len + 1, ".~%u~", n);
    return out;
}

static char *
ln_pick_numbered_backup_path(const char *dst, ln_backup_mode_t mode, const char *suffix)
{
    bool saw_numbered;
    unsigned n;

    saw_numbered = false;
    for (n = 1; n < 100000U; ++n) {
        char *candidate;

        candidate = ln_backup_numbered_path(dst, n);
        if (!candidate) {
            return NULL;
        }

        if (ln_exists_lstat(candidate)) {
            saw_numbered = true;
            free(candidate);
            continue;
        }

        if (mode == LN_BACKUP_NUMBERED) {
            return candidate;
        }

        if (mode == LN_BACKUP_EXISTING) {
            if (saw_numbered) {
                return candidate;
            }
            free(candidate);
            return ln_backup_simple_path(dst, suffix);
        }

        free(candidate);
        break;
    }

    if (mode == LN_BACKUP_NUMBERED) {
        errno = ENOSPC;
        return NULL;
    }

    return ln_backup_simple_path(dst, suffix);
}

static char *
ln_backup_pick_path(const ln_options_t *opts, const char *dst)
{
    const char *suffix;

    suffix = opts->backup_suffix;
    if (!suffix || *suffix == '\0') {
        suffix = getenv("SIMPLE_BACKUP_SUFFIX");
    }
    if (!suffix || *suffix == '\0') {
        suffix = "~";
    }

    if (opts->backup_mode == LN_BACKUP_SIMPLE) {
        return ln_backup_simple_path(dst, suffix);
    }

    return ln_pick_numbered_backup_path(dst, opts->backup_mode, suffix);
}

int
ln_backup_destination(const ln_options_t *opts, const char *dst)
{
    char *backup_path;

    if (opts->backup_mode == LN_BACKUP_NONE) {
        return 0;
    }

    backup_path = ln_backup_pick_path(opts, dst);
    if (!backup_path) {
        return -1;
    }

    if (rename(dst, backup_path) != 0) {
        free(backup_path);
        return -1;
    }

    free(backup_path);
    return 0;
}
