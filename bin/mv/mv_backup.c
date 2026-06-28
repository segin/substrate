#include "mv_backup.h"
#include "mv_path.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool target_exists(const char *target)
{
    struct stat st;
    return target != NULL && lstat(target, &st) == 0;
}

static bool numbered_backups_exist(const char *target)
{
    char *dir;
    const char *base;
    const char *slash;
    DIR *dp;
    struct dirent *de;
    size_t baselen;
    bool found = false;

    slash = strrchr(target, '/');
    if (slash != NULL) {
        size_t dl = (size_t)(slash - target);
        if (dl == 0) {
            dir = strdup("/");
        } else {
            dir = strndup(target, dl);
        }
        base = slash + 1;
    } else {
        dir = strdup(".");
        base = target;
    }
    if (dir == NULL) {
        return false;
    }
    baselen = strlen(base);

    dp = opendir(dir);
    free(dir);
    if (dp == NULL) {
        return false;
    }
    while ((de = readdir(dp)) != NULL) {
        size_t nlen = strlen(de->d_name);
        /* Match BASE.~N~ with N a positive integer. */
        if (nlen <= baselen + 3u) {
            continue;
        }
        if (strncmp(de->d_name, base, baselen) != 0) {
            continue;
        }
        if (de->d_name[baselen] != '.' ||
            de->d_name[baselen + 1] != '~') {
            continue;
        }
        if (de->d_name[nlen - 1] != '~') {
            continue;
        }
        const char *digits = de->d_name + baselen + 2;
        size_t dl = nlen - (baselen + 2) - 1;
        if (dl == 0) {
            continue;
        }
        bool all_digits = true;
        for (size_t i = 0; i < dl; i++) {
            if (digits[i] < '0' || digits[i] > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            found = true;
            break;
        }
    }
    closedir(dp);
    return found;
}

static char *next_numbered_backup(const char *target)
{
    unsigned long highest = 0;
    char *dir;
    const char *base;
    const char *slash;
    DIR *dp;
    struct dirent *de;
    size_t baselen;
    char *out;

    slash = strrchr(target, '/');
    if (slash != NULL) {
        size_t dl = (size_t)(slash - target);
        dir = (dl == 0) ? strdup("/") : strndup(target, dl);
        base = slash + 1;
    } else {
        dir = strdup(".");
        base = target;
    }
    if (dir == NULL) {
        return NULL;
    }
    baselen = strlen(base);

    dp = opendir(dir);
    if (dp != NULL) {
        while ((de = readdir(dp)) != NULL) {
            size_t nlen = strlen(de->d_name);
            if (nlen <= baselen + 3u) continue;
            if (strncmp(de->d_name, base, baselen) != 0) continue;
            if (de->d_name[baselen] != '.' ||
                de->d_name[baselen + 1] != '~') continue;
            if (de->d_name[nlen - 1] != '~') continue;
            const char *digits = de->d_name + baselen + 2;
            size_t dl = nlen - (baselen + 2) - 1;
            char tmp[32];
            if (dl == 0 || dl >= sizeof(tmp)) continue;
            bool ok = true;
            for (size_t i = 0; i < dl; i++) {
                if (digits[i] < '0' || digits[i] > '9') { ok = false; break; }
                tmp[i] = digits[i];
            }
            tmp[dl] = '\0';
            if (!ok) continue;
            unsigned long n = strtoul(tmp, NULL, 10);
            if (n > highest) highest = n;
        }
        closedir(dp);
    }
    free(dir);

    size_t out_size = strlen(target) + 32;
    out = malloc(out_size);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, out_size, "%s.~%lu~", target, highest + 1u);
    return out;
}

static char *simple_backup(const char *target, const char *suffix)
{
    size_t tlen = strlen(target);
    size_t slen = strlen(suffix);
    char *out = malloc(tlen + slen + 1u);
    if (out == NULL) return NULL;
    memcpy(out, target, tlen);
    memcpy(out + tlen, suffix, slen + 1u);
    return out;
}

char *mv_backup_name(const char *target,
                     enum mv_backup_mode mode,
                     const char *suffix)
{
    if (target == NULL || mode == MV_BACKUP_NONE) {
        return NULL;
    }
    if (!target_exists(target)) {
        return NULL;
    }
    if (suffix == NULL || suffix[0] == '\0') {
        suffix = "~";
    }
    switch (mode) {
    case MV_BACKUP_SIMPLE:
        return simple_backup(target, suffix);
    case MV_BACKUP_NUMBERED:
        return next_numbered_backup(target);
    case MV_BACKUP_EXISTING:
        if (numbered_backups_exist(target)) {
            return next_numbered_backup(target);
        }
        return simple_backup(target, suffix);
    case MV_BACKUP_NONE:
    default:
        return NULL;
    }
}

int mv_perform_backup(const char *target,
                      const struct mv_options *opts,
                      char **backup_path_out)
{
    char *bname;

    if (opts == NULL || opts->backup == MV_BACKUP_NONE) {
        if (backup_path_out) *backup_path_out = NULL;
        return 0;
    }
    bname = mv_backup_name(target, opts->backup, opts->backup_suffix);
    if (bname == NULL) {
        /* Either target absent or alloc failed.  Distinguish via errno? */
        if (backup_path_out) *backup_path_out = NULL;
        return 0;
    }
    if (rename(target, bname) != 0) {
        int err = errno;
        free(bname);
        errno = err;
        return -1;
    }
    if (backup_path_out) {
        *backup_path_out = bname;
    } else {
        free(bname);
    }
    return 0;
}
