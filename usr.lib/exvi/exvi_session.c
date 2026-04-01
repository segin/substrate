#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char **ex_args = NULL;
static int ex_argc = 0;
static int ex_arg_idx = 0;
static int ex_args_owned = 0;

typedef struct {
    char *filename;
    int line;
} tag_frame_t;

static tag_frame_t *tag_stack = NULL;
static int tag_stack_len = 0;

static void
free_ex_arglist(void)
{
    if (!ex_args_owned || !ex_args) {
        ex_args = NULL;
        ex_argc = 0;
        ex_arg_idx = 0;
        ex_args_owned = 0;
        return;
    }

    for (int i = 0; i < ex_argc; i++) {
        free(ex_args[i]);
    }
    free(ex_args);
    ex_args = NULL;
    ex_argc = 0;
    ex_arg_idx = 0;
    ex_args_owned = 0;
}

static int
set_ex_arglist_from_words(const char *text)
{
    char **new_args = NULL;
    int new_argc = 0;
    const char *p = text;

    while (*p) {
        char *word;
        char **grown;
        const char *start;
        size_t len;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        len = (size_t)(p - start);
        word = malloc(len + 1);
        if (!word) {
            goto fail;
        }
        memcpy(word, start, len);
        word[len] = '\0';

        grown = realloc(new_args, sizeof(*new_args) * (size_t)(new_argc + 1));
        if (!grown) {
            free(word);
            goto fail;
        }
        new_args = grown;
        new_args[new_argc++] = word;
    }

    if (new_argc == 0) {
        return -1;
    }

    free_ex_arglist();
    ex_args = new_args;
    ex_argc = new_argc;
    ex_arg_idx = 0;
    ex_args_owned = 1;
    return 0;

fail:
    if (new_args) {
        for (int i = 0; i < new_argc; i++) {
            free(new_args[i]);
        }
    }
    free(new_args);
    return -1;
}

static void
load_current_arg_file(buffer_t *b)
{
    replace_saved_string(&alternate_filename, b->filename);
    buf_free(b);
    buf_init(b);
    b->filename = strdup(ex_args[ex_arg_idx]);
    if (b->filename) {
        buf_read_file(b, b->filename);
        if (!batch_mode) {
            printf("\"%s\" %d lines\n", b->filename, b->line_count);
        }
    }
}

static void
free_tag_stack(void)
{
    for (int i = 0; i < tag_stack_len; i++) {
        free(tag_stack[i].filename);
    }
    free(tag_stack);
    tag_stack = NULL;
    tag_stack_len = 0;
}

static int
push_tag_frame(buffer_t *b)
{
    tag_frame_t *grown;
    char *filename;

    if (!b->filename) {
        return 0;
    }

    filename = strdup(b->filename);
    if (!filename) {
        return -1;
    }

    grown = realloc(tag_stack, sizeof(*tag_stack) * (size_t)(tag_stack_len + 1));
    if (!grown) {
        free(filename);
        return -1;
    }
    tag_stack = grown;
    tag_stack[tag_stack_len].filename = filename;
    tag_stack[tag_stack_len].line = buf_current_line(b);
    tag_stack_len++;
    return 0;
}

static const char *
tag_display_filename(buffer_t *b)
{
    if (b->filename && *b->filename) {
        return b->filename;
    }
    return "[No Name]";
}

static FILE *
open_tags_file(char **path_out)
{
    const char *spec = (option_tags && *option_tags) ? option_tags : EXVI_DEFAULT_TAGS;
    const char *p = spec;

    if (path_out) {
        *path_out = NULL;
    }

    while (*p) {
        const char *start;
        const char *end;
        char *path;
        FILE *f;

        while (*p == ',' || isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        start = p;
        while (*p && *p != ',') {
            p++;
        }
        end = p;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }
        if (end == start) {
            continue;
        }

        path = malloc((size_t)(end - start) + 1);
        if (!path) {
            break;
        }
        memcpy(path, start, (size_t)(end - start));
        path[end - start] = '\0';

        f = fopen(path, "r");
        if (f) {
            if (path_out) {
                *path_out = path;
            } else {
                free(path);
            }
            return f;
        }
        free(path);
    }

    return NULL;
}

void
exvi_cleanup_session_state(void)
{
    free_ex_arglist();
    free_tag_stack();
}

void
exvi_set_cli_arglist(int argc, char **argv, int optind)
{
    ex_args = argv + optind;
    ex_argc = argc - optind;
    ex_arg_idx = 0;
    ex_args_owned = 0;
}

void
exvi_set_owned_arglist(char **args, int argc)
{
    free_ex_arglist();
    ex_args = args;
    ex_argc = argc;
    ex_arg_idx = 0;
    ex_args_owned = 1;
}

int
exvi_has_arglist(void)
{
    return ex_argc > 0;
}

const char *
exvi_current_arg(void)
{
    if (ex_argc == 0 || ex_arg_idx < 0 || ex_arg_idx >= ex_argc) {
        return NULL;
    }
    return ex_args[ex_arg_idx];
}

int
handle_pop_command(buffer_t *b, int force)
{
    tag_frame_t frame;

    if (b->modified && !force) {
        fprintf(stderr, "No write since last change (add ! to override)\n");
        return 1;
    }
    if (tag_stack_len == 0) {
        fprintf(stderr, "Tag stack empty\n");
        return 1;
    }

    frame = tag_stack[--tag_stack_len];
    if (tag_stack_len == 0) {
        free(tag_stack);
        tag_stack = NULL;
    }
    replace_saved_string(&alternate_filename, b->filename);
    buf_free(b);
    buf_init(b);
    b->filename = frame.filename;
    buf_read_file(b, b->filename);
    if (frame.line > 0) {
        b->cur = buf_get_line(b, frame.line);
    }
    if (!batch_mode) {
        printf("\"%s\" %d lines\n", b->filename, b->line_count);
    }
    return 1;
}

int
handle_tags_command(buffer_t *b)
{
    if (tag_stack_len == 0) {
        printf("Tag stack empty\n");
        return 1;
    }

    for (int i = 0; i < tag_stack_len; i++) {
        printf("%d %s:%d\n", i + 1, tag_stack[i].filename, tag_stack[i].line);
    }
    printf("> %s:%d\n", tag_display_filename(b), buf_current_line(b));
    return 1;
}

int
handle_tag_command(buffer_t *b, const char *args, void (*command_fn)(buffer_t *, char *))
{
    char *ptr = (char *)args;
    FILE *f;
    char *line = NULL;
    char *tags_path = NULL;
    size_t cap = 0;
    ssize_t ret;
    int found = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (!*ptr) {
        exvi_report_error("Usage: tag <name>");
        return 1;
    }

    f = open_tags_file(&tags_path);
    if (!f) {
        exvi_report_error("No tags file");
        return 1;
    }

    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret - 1] == '\n') {
            line[ret - 1] = '\0';
        }
        char *tname = strtok(line, "\t ");
        char *tfile = strtok(NULL, "\t ");
        char *tcmd = strtok(NULL, "");

        if (tname && tfile && tcmd && strcmp(tname, ptr) == 0) {
            char *old_filename;

            if (b->modified) {
                exvi_report_error("No write since last change");
                free(line);
                fclose(f);
                return 1;
            }
            if (push_tag_frame(b) != 0) {
                fprintf(stderr, "Out of memory\n");
                free(line);
                fclose(f);
                return 1;
            }
            old_filename = b->filename ? strdup(b->filename) : NULL;
            buf_free(b);
            buf_init(b);
            b->filename = strdup(tfile);
            buf_read_file(b, b->filename);
            replace_saved_string(&alternate_filename, old_filename);
            free(old_filename);
            if (!batch_mode) {
                printf("\"%s\" %d lines\n", b->filename, b->line_count);
            }

            {
                char *tcmt = strstr(tcmd, ";\"");
                if (tcmt) {
                    *tcmt = '\0';
                }
            }
            command_fn(b, tcmd);
            found = 1;
            break;
        }
    }
    free(line);
    fclose(f);
    free(tags_path);
    if (!found) {
        exvi_report_errorf("Tag not found: %s", ptr);
    }
    return 1;
}

int
handle_set_command(const char *args)
{
    char *ptr = (char *)args;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    if (!*ptr) {
        if (option_number) {
            printf("number\n");
        }
        if (option_list) {
            printf("list\n");
        }
        if (option_ignorecase) {
            printf("ignorecase\n");
        }
        if (option_readonly) {
            printf("readonly\n");
        }
        if (option_wrapscan) {
            printf("wrapscan\n");
        }
        if (option_tabstop != EXVI_DEFAULT_TABSTOP) {
            printf("tabstop=%d\n", option_tabstop);
        }
        if (option_tags && strcmp(option_tags, EXVI_DEFAULT_TAGS) != 0) {
            printf("tags=%s\n", option_tags);
        }
        return 1;
    }

    while (*ptr) {
        char *end = ptr;
        char *eq;
        size_t len;
        size_t value_len = 0;
        int query = 0;
        const char *value = NULL;

        while (*end && !isspace((unsigned char)*end)) {
            end++;
        }
        len = (size_t)(end - ptr);
        if (len > 0 && end[-1] == '?') {
            query = 1;
            len--;
        }
        eq = memchr(ptr, '=', len);
        if (eq) {
            value = eq + 1;
            value_len = (size_t)(end - value);
            len = (size_t)(eq - ptr);
        }

        if (len == 3 && strncmp(ptr, "all", 3) == 0) {
            if (query || eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (option_number) {
                printf("number\n");
            }
            if (option_list) {
                printf("list\n");
            }
            if (option_ignorecase) {
                printf("ignorecase\n");
            }
            if (option_readonly) {
                printf("readonly\n");
            }
            if (option_wrapscan) {
                printf("wrapscan\n");
            }
            printf("tabstop=%d\n", option_tabstop);
            printf("tags=%s\n", option_tags ? option_tags : EXVI_DEFAULT_TAGS);
        } else if (len == 2 && strncmp(ptr, "ic", 2) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_ignorecase ? "ignorecase" : "noignorecase");
            } else {
                option_ignorecase = 1;
            }
        } else if (len == 10 && strncmp(ptr, "ignorecase", 10) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_ignorecase ? "ignorecase" : "noignorecase");
            } else {
                option_ignorecase = 1;
            }
        } else if (len == 4 && strncmp(ptr, "noic", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_ignorecase ? "noignorecase" : "ignorecase");
            } else {
                option_ignorecase = 0;
            }
        } else if (len == 12 && strncmp(ptr, "noignorecase", 12) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_ignorecase ? "noignorecase" : "ignorecase");
            } else {
                option_ignorecase = 0;
            }
        } else if (len == 2 && strncmp(ptr, "ro", 2) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_readonly ? "readonly" : "noreadonly");
            } else {
                option_readonly = 1;
            }
        } else if (len == 8 && strncmp(ptr, "readonly", 8) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_readonly ? "readonly" : "noreadonly");
            } else {
                option_readonly = 1;
            }
        } else if (len == 4 && strncmp(ptr, "noro", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_readonly ? "noreadonly" : "readonly");
            } else {
                option_readonly = 0;
            }
        } else if (len == 10 && strncmp(ptr, "noreadonly", 10) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_readonly ? "noreadonly" : "readonly");
            } else {
                option_readonly = 0;
            }
        } else if (len == 2 && strncmp(ptr, "nu", 2) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_number ? "number" : "nonumber");
            } else {
                option_number = 1;
            }
        } else if (len == 6 && strncmp(ptr, "number", 6) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_number ? "number" : "nonumber");
            } else {
                option_number = 1;
            }
        } else if (len == 4 && strncmp(ptr, "nonu", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_number ? "nonumber" : "number");
            } else {
                option_number = 0;
            }
        } else if (len == 8 && strncmp(ptr, "nonumber", 8) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_number ? "nonumber" : "number");
            } else {
                option_number = 0;
            }
        } else if (len == 2 && strncmp(ptr, "li", 2) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_list ? "list" : "nolist");
            } else {
                option_list = 1;
            }
        } else if (len == 4 && strncmp(ptr, "list", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_list ? "list" : "nolist");
            } else {
                option_list = 1;
            }
        } else if (len == 4 && strncmp(ptr, "noli", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_list ? "nolist" : "list");
            } else {
                option_list = 0;
            }
        } else if (len == 6 && strncmp(ptr, "nolist", 6) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_list ? "nolist" : "list");
            } else {
                option_list = 0;
            }
        } else if (len == 2 && strncmp(ptr, "ws", 2) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_wrapscan ? "wrapscan" : "nowrapscan");
            } else {
                option_wrapscan = 1;
            }
        } else if (len == 8 && strncmp(ptr, "wrapscan", 8) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_wrapscan ? "wrapscan" : "nowrapscan");
            } else {
                option_wrapscan = 1;
            }
        } else if (len == 4 && strncmp(ptr, "nows", 4) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_wrapscan ? "nowrapscan" : "wrapscan");
            } else {
                option_wrapscan = 0;
            }
        } else if (len == 10 && strncmp(ptr, "nowrapscan", 10) == 0) {
            if (eq) {
                exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
                return 1;
            }
            if (query) {
                printf("%s\n", option_wrapscan ? "nowrapscan" : "wrapscan");
            } else {
                option_wrapscan = 0;
            }
        } else if ((len == 2 && strncmp(ptr, "ts", 2) == 0)
                || (len == 7 && strncmp(ptr, "tabstop", 7) == 0)) {
            if (query || (!eq && !query)) {
                printf("tabstop=%d\n", option_tabstop);
            } else {
                char *num_end = NULL;
                char buf[32];
                long val;

                if (!value || value_len == 0 || value_len >= sizeof(buf)) {
                    exvi_report_error("Bad tabstop value");
                    return 1;
                }
                memcpy(buf, value, value_len);
                buf[value_len] = '\0';
                val = strtol(buf, &num_end, 10);
                if (!num_end || *num_end != '\0' || val < EXVI_MIN_TABSTOP
                        || val > EXVI_MAX_TABSTOP) {
                    exvi_report_error("Bad tabstop value");
                    return 1;
                }
                option_tabstop = (int)val;
            }
        } else if (len == 4 && strncmp(ptr, "tags", 4) == 0) {
            if (query || (!eq && !query)) {
                printf("tags=%s\n", option_tags ? option_tags : EXVI_DEFAULT_TAGS);
            } else {
                char *copy;

                if (!value || value_len == 0) {
                    exvi_report_error("Bad tags value");
                    return 1;
                }
                copy = malloc(value_len + 1);
                if (!copy) {
                    perror("malloc");
                    return 1;
                }
                memcpy(copy, value, value_len);
                copy[value_len] = '\0';
                replace_saved_string(&option_tags, copy);
                free(copy);
            }
        } else {
            exvi_report_errorf("Unknown option: %.*s", (int)(end - ptr), ptr);
            return 1;
        }

        ptr = end;
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
    }
    return 1;
}

int
exvi_write_allowed(buffer_t *b, const char *filename, int force)
{
    if (force || !option_readonly) {
        return 1;
    }
    if (filename && b->filename && strcmp(filename, b->filename) != 0) {
        return 1;
    }
    exvi_report_error("File is read only (add ! to override)");
    return 0;
}

int
handle_args_command(const char *args)
{
    const char *ptr = args;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr) {
        if (set_ex_arglist_from_words(ptr) != 0) {
            exvi_report_error("Usage: args file ...");
            return 1;
        }
    }
    if (ex_argc == 0) {
        printf("No files\n");
        return 1;
    }
    for (int i = 0; i < ex_argc; i++) {
        if (i == ex_arg_idx) {
            printf("[%s] ", ex_args[i]);
        } else {
            printf("%s ", ex_args[i]);
        }
    }
    printf("\n");
    return 1;
}

int
handle_next_command(buffer_t *b, const char *args, int force)
{
    int replaced_args = 0;

    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }
    if (args) {
        const char *ptr = args;

        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if (*ptr) {
            if (set_ex_arglist_from_words(ptr) != 0) {
                exvi_report_error("Usage: next [file ...]");
                return 1;
            }
            replaced_args = 1;
        }
    }
    if (replaced_args) {
        load_current_arg_file(b);
        return 1;
    }
    if (ex_arg_idx + 1 >= ex_argc) {
        exvi_report_error("No more files");
        return 1;
    }
    ex_arg_idx++;
    load_current_arg_file(b);
    return 1;
}

int
handle_prev_command(buffer_t *b, int force)
{
    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }
    if (ex_arg_idx - 1 < 0) {
        exvi_report_error("No previous files");
        return 1;
    }
    ex_arg_idx--;
    load_current_arg_file(b);
    return 1;
}

int
handle_rewind_command(buffer_t *b, int force)
{
    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }
    if (ex_argc == 0) {
        exvi_report_error("No files");
        return 1;
    }
    ex_arg_idx = 0;
    load_current_arg_file(b);
    return 1;
}

int
handle_preserve_command(buffer_t *b)
{
    if (b->filename && b->modified && b->head) {
        char *path = recover_path_for(b->filename);

        if (!path) {
            exvi_report_error("Out of memory");
            return 1;
        }
        buf_write_file(b, path, 0);
        printf("File preserved as %s\n", path);
        free(path);
    } else {
        exvi_report_error("No modifications or filename to preserve");
    }
    return 1;
}

int
handle_recover_command(buffer_t *b, const char *args)
{
    char *recover_name;
    char *ptr = (char *)args;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr) {
        recover_name = expand_filename_refs(b, ptr);
    } else if (b->filename) {
        recover_name = expand_filename_refs(b, b->filename);
    } else {
        exvi_report_error("No current filename");
        return 1;
    }
    if (!recover_name) {
        return 1;
    }
    if (load_recover_into_buffer(b, recover_name) != 0) {
        exvi_report_errorf("No recover file for %s", recover_name);
        free(recover_name);
        return 1;
    }
    free(recover_name);
    printf("\"%s\" recovered, %d lines\n", b->filename, b->line_count);
    return 1;
}

int
handle_write_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2, int force)
{
    char *ptr = (char *)args;
    char *target = NULL;
    int append = 0;
    int write_addr1 = 1;
    int write_addr2 = b->line_count;

    if (explicit_range) {
        write_addr1 = addr1;
        write_addr2 = addr2;
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr == '>' && *(ptr + 1) == '>') {
        append = 1;
        ptr += 2;
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
    }
    if (*ptr) {
        target = expand_filename_refs(b, ptr);
        if (!target) {
            return 1;
        }
        if (!exvi_write_allowed(b, target, force)) {
            free(target);
            return 1;
        }
        buf_write_range(b, target, append, write_addr1, write_addr2);
        free(target);
    } else if (b->filename) {
        if (!exvi_write_allowed(b, b->filename, force)) {
            return 1;
        }
        buf_write_range(b, b->filename, append, write_addr1, write_addr2);
    } else {
        exvi_report_error("No current filename");
    }
    return 1;
}

int
handle_edit_command(buffer_t *b, const char *args, int force)
{
    char *ptr = (char *)args;
    char *old_filename = NULL;
    char *new_filename = NULL;
    int replace_alt = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }

    if (b->filename) {
        old_filename = strdup(b->filename);
        if (!old_filename) {
            perror("strdup");
            return 1;
        }
    }

    if (!*ptr && old_filename) {
        new_filename = strdup(old_filename);
        if (!new_filename) {
            free(old_filename);
            perror("strdup");
            return 1;
        }
    } else if (*ptr) {
        new_filename = expand_filename_refs(b, ptr);
        if (!new_filename) {
            free(old_filename);
            return 1;
        }
    }

    if (old_filename && new_filename && strcmp(old_filename, new_filename) != 0) {
        replace_alt = 1;
    }

    buf_free(b);
    buf_init(b);
    b->filename = new_filename;
    if (replace_alt) {
        replace_saved_string(&alternate_filename, old_filename);
    }
    if (b->filename) {
        buf_read_file(b, b->filename);
        if (!batch_mode) {
            printf("\"%s\" %d lines\n", b->filename, b->line_count);
        }
    } else {
        exvi_report_error("No current filename");
    }
    free(old_filename);
    return 1;
}

int
handle_read_command(buffer_t *b, const char *args, int addr2)
{
    char *ptr = (char *)args;
    const char *display_name;
    char *expanded_name = NULL;
    FILE *f = NULL;
    int is_pipe = 0;
    int replace_empty_line = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    if (*ptr == '!') {
        if (secure_mode) {
            exvi_report_shell_forbidden();
            return 1;
        }
        is_pipe = 1;
        display_name = ptr + 1;
        f = popen(ptr + 1, "r");
    } else {
        if (!*ptr) {
            ptr = b->filename;
        }
        if (ptr) {
            expanded_name = expand_filename_refs(b, ptr);
            if (!expanded_name) {
                return 1;
            }
        }
        display_name = expanded_name;
        if (expanded_name) {
            f = fopen(expanded_name, "r");
        }
    }

    if (f) {
        int lines_read = 0;
        char *line = NULL;
        size_t cap = 0;
        ssize_t ret;
        int dest = default_read_destination(b, addr2);
        line_t *pos = buf_get_line(b, dest);

        if (addr2 == -1 && b->empty_origin && b->line_count == 1 &&
            b->head == b->tail && b->head && b->head->len == 0) {
            replace_empty_line = 1;
            pos = NULL;
        }

        while ((ret = getline(&line, &cap, f)) != -1) {
            if (ret > 0 && line[ret - 1] == '\n') {
                line[ret - 1] = '\0';
            }
            if (replace_empty_line) {
                char *text = strdup(line);

                if (!text) {
                    free(line);
                    if (is_pipe) {
                        pclose(f);
                    } else {
                        fclose(f);
                    }
                    free(expanded_name);
                    perror("strdup");
                    return 1;
                }
                free(b->head->text);
                b->head->text = text;
                b->head->len = strlen(text);
                b->cur = b->head;
                b->modified = 1;
                b->empty_origin = 0;
                pos = b->head;
                replace_empty_line = 0;
            } else {
                pos = buf_insert_after(b, pos, line);
            }
            lines_read++;
        }
        free(line);

        if (is_pipe) {
            pclose(f);
        } else {
            fclose(f);
        }

        if (!batch_mode && display_name) {
            printf("\"%s\" %d lines\n", display_name, lines_read);
        }
        free(expanded_name);
    } else {
        perror(expanded_name ? expanded_name : ptr);
        free(expanded_name);
    }
    return 1;
}
