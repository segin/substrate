#include "rm_safety.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *
rm_dup_range(const char *text, size_t length)
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
rm_getcwd_dynamic(void)
{
    size_t size;

    size = 128;
    for (;;) {
        char *buffer;

        buffer = (char *)malloc(size);
        if (buffer == NULL) {
            return NULL;
        }
        if (getcwd(buffer, size) != NULL) {
            return buffer;
        }
        free(buffer);
        if (errno != ERANGE || size > ((size_t)-1) / 2) {
            return NULL;
        }
        size *= 2;
    }
}

bool
rm_operand_is_dot_or_dotdot(const char *path)
{
    size_t end;
    size_t start;
    size_t name_length;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        --end;
    }
    start = end;
    while (start > 0 && path[start - 1] != '/') {
        --start;
    }
    name_length = end - start;
    return (name_length == 1 && path[start] == '.') ||
        (name_length == 2 && path[start] == '.' && path[start + 1] == '.');
}

char *
rm_normalize_path(const char *path)
{
    char *combined;
    char *normalized;
    char *working_directory = NULL;
    size_t *component_stack;
    size_t combined_len;
    size_t combined_pos;
    size_t normalized_len;
    size_t stack_depth;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    if (path[0] == '/') {
        combined = rm_dup_range(path, strlen(path));
    } else {
        size_t cwd_len;
        size_t path_len;

        working_directory = rm_getcwd_dynamic();
        if (working_directory == NULL) {
            return NULL;
        }
        cwd_len = strlen(working_directory);
        path_len = strlen(path);
        combined = (char *)malloc(cwd_len + 1 + path_len + 1);
        if (combined == NULL) {
            free(working_directory);
            return NULL;
        }
        memcpy(combined, working_directory, cwd_len);
        combined[cwd_len] = '/';
        memcpy(combined + cwd_len + 1, path, path_len + 1);
    }

    combined_len = strlen(combined);
    normalized = (char *)malloc(combined_len + 2);
    component_stack = (size_t *)malloc((combined_len + 1) *
        sizeof(*component_stack));
    if (normalized == NULL || component_stack == NULL) {
        free(component_stack);
        free(normalized);
        free(combined);
        free(working_directory);
        return NULL;
    }

    normalized[0] = '/';
    normalized[1] = '\0';
    normalized_len = 1;
    stack_depth = 0;

    combined_pos = 0;
    while (combined[combined_pos] != '\0') {
        size_t component_start;
        size_t component_end;
        size_t component_length;

        while (combined[combined_pos] == '/') {
            ++combined_pos;
        }
        if (combined[combined_pos] == '\0') {
            break;
        }

        component_start = combined_pos;
        while (combined[combined_pos] != '\0' && combined[combined_pos] != '/') {
            ++combined_pos;
        }
        component_end = combined_pos;
        component_length = component_end - component_start;

        if (component_length == 1 &&
            strncmp(combined + component_start, ".", component_length) == 0) {
            continue;
        }
        if (component_length == 2 &&
            strncmp(combined + component_start, "..", component_length) == 0) {
            if (stack_depth != 0) {
                normalized_len = component_stack[--stack_depth];
                normalized[normalized_len] = '\0';
                if (normalized_len == 0) {
                    normalized[0] = '/';
                    normalized[1] = '\0';
                    normalized_len = 1;
                }
            }
            continue;
        }

        component_stack[stack_depth++] = normalized_len;
        if (normalized_len > 1) {
            normalized[normalized_len++] = '/';
        }
        memcpy(normalized + normalized_len, combined + component_start,
            component_length);
        normalized_len += component_length;
        normalized[normalized_len] = '\0';
    }

    free(component_stack);
    free(combined);
    free(working_directory);
    return normalized;
}

int
rm_split_path(const char *path, char **parent_out, char **name_out,
    char **display_out, bool *had_trailing_slash)
{
    char *trimmed;
    const char *last_slash;
    size_t length;

    if (path == NULL || path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    *parent_out = NULL;
    *name_out = NULL;
    *display_out = NULL;
    if (had_trailing_slash != NULL) {
        *had_trailing_slash = false;
    }

    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') {
        if (had_trailing_slash != NULL) {
            *had_trailing_slash = true;
        }
        --length;
    }

    trimmed = rm_dup_range(path, length);
    if (trimmed == NULL) {
        return -1;
    }
    *display_out = rm_dup_range(trimmed, strlen(trimmed));
    if (*display_out == NULL) {
        free(trimmed);
        return -1;
    }

    if (strcmp(trimmed, "/") == 0) {
        *name_out = trimmed;
        return 0;
    }

    last_slash = strrchr(trimmed, '/');
    if (last_slash == NULL) {
        *name_out = trimmed;
        return 0;
    }
    if (last_slash == trimmed) {
        *parent_out = rm_dup_range("/", 1);
        *name_out = rm_dup_range(trimmed + 1, strlen(trimmed + 1));
        free(trimmed);
    } else {
        size_t parent_len;

        parent_len = (size_t)(last_slash - trimmed);
        *parent_out = rm_dup_range(trimmed, parent_len);
        *name_out = rm_dup_range(last_slash + 1, strlen(last_slash + 1));
        free(trimmed);
    }

    if (*name_out == NULL || (last_slash != NULL && *parent_out == NULL)) {
        free(*parent_out);
        free(*name_out);
        free(*display_out);
        *parent_out = NULL;
        *name_out = NULL;
        *display_out = NULL;
        return -1;
    }
    return 0;
}

const char *
rm_file_type_name(mode_t mode)
{
    if (S_ISDIR(mode)) {
        return "directory";
    }
    if (S_ISLNK(mode)) {
        return "symbolic link";
    }
    if (S_ISFIFO(mode)) {
        return "fifo";
    }
    if (S_ISSOCK(mode)) {
        return "socket";
    }
    if (S_ISBLK(mode)) {
        return "block device";
    }
    if (S_ISCHR(mode)) {
        return "character device";
    }
    return "file";
}

FILE *
rm_open_prompt_stream(void)
{
    if (isatty(STDIN_FILENO)) {
        return stdin;
    }
    return NULL;
}

int
rm_prompt_string(FILE *input, const char *question)
{
    char answer[32];

    if (input == NULL) {
        return 0;
    }

    fputs(question, stderr);
    fflush(stderr);
    if (fgets(answer, sizeof(answer), input) == NULL) {
        clearerr(input);
        return 0;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
}

int
rm_prompt_removal(FILE *input, bool write_protected, const char *type_name,
    const char *path)
{
    char question[1024];

    snprintf(question, sizeof(question),
        write_protected ? "rm: remove write-protected %s '%s'? " :
        "rm: remove %s '%s'? ",
        type_name, path);
    return rm_prompt_string(input, question);
}

bool
rm_target_is_write_protected(const struct stat *target_st,
    const struct stat *parent_st)
{
    mode_t write_mask;
    uid_t effective_uid;
    gid_t effective_gid;

    if (target_st == NULL) {
        return false;
    }
    if (S_ISLNK(target_st->st_mode)) {
        return false;
    }

    effective_uid = geteuid();
    effective_gid = getegid();

    if (effective_uid == 0) {
        return false;
    }
    if (effective_uid == target_st->st_uid) {
        write_mask = S_IWUSR;
    } else if (effective_gid == target_st->st_gid) {
        write_mask = S_IWGRP;
    } else {
        write_mask = S_IWOTH;
    }
    if ((target_st->st_mode & write_mask) == 0) {
        return true;
    }

    if (parent_st != NULL && (parent_st->st_mode & S_ISVTX) != 0 &&
        effective_uid != parent_st->st_uid &&
        effective_uid != target_st->st_uid) {
        return true;
    }

    return false;
}