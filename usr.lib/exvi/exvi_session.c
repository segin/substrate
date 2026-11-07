#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

static char **ex_args = NULL;
static int ex_argc = 0;
static int ex_arg_idx = 0;
static int ex_args_owned = 0;

typedef struct {
    char *tagname;
    char *filename;
    int line;
} tag_frame_t;

static tag_frame_t *tag_stack = NULL;
static int tag_stack_len = 0;
static char *current_tag_name = NULL;

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
        exvi_set_loaded_line(b);
        if (!batch_mode) {
            printf("\"%s\" %d lines\n", b->filename, b->line_count);
        }
    }
}

static void
free_tag_stack(void)
{
    for (int i = 0; i < tag_stack_len; i++) {
        free(tag_stack[i].tagname);
        free(tag_stack[i].filename);
    }
    free(tag_stack);
    tag_stack = NULL;
    tag_stack_len = 0;
    replace_saved_string(&current_tag_name, NULL);
}

static int
push_tag_frame(buffer_t *b)
{
    tag_frame_t *grown;
    char *tagname = NULL;
    char *filename;

    if (!b->filename) {
        return 0;
    }

    filename = strdup(b->filename);
    if (!filename) {
        return -1;
    }
    if (current_tag_name) {
        tagname = strdup(current_tag_name);
        if (!tagname) {
            free(filename);
            return -1;
        }
    }

    grown = realloc(tag_stack, sizeof(*tag_stack) * (size_t)(tag_stack_len + 1));
    if (!grown) {
        free(tagname);
        free(filename);
        return -1;
    }
    tag_stack = grown;
    tag_stack[tag_stack_len].tagname = tagname;
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

/**
 * Open the next readable file of the tags option.
 * @param cursor where in the option to go on from: NULL at first, then as
 *        this call leaves it
 * @param path_out its path (to free), or NULL
 * @return the file, or NULL when no more can be opened
 */
static FILE *
open_tags_file(const char **cursor, char **path_out)
{
    const char *spec = (option_tags && *option_tags) ? option_tags : EXVI_DEFAULT_TAGS;
    const char *p = *cursor ? *cursor : spec;

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
            *cursor = p;
            return f;
        }
        free(path);
    }

    *cursor = p;
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
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }
    if (tag_stack_len == 0) {
        exvi_report_error("Tag stack empty");
        return 1;
    }

    frame = tag_stack[--tag_stack_len];
    if (exvi_restricted_filename_change(b, frame.filename)) {
        tag_stack[tag_stack_len++] = frame;
        return 1;
    }
    if (tag_stack_len == 0) {
        free(tag_stack);
        tag_stack = NULL;
    }
    replace_saved_string(&current_tag_name, frame.tagname);
    free(frame.tagname);
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
        if (!current_tag_name) {
            printf("Tag stack empty\n");
            return 1;
        }
    }

    for (int i = 0; i < tag_stack_len; i++) {
        if (tag_stack[i].tagname && *tag_stack[i].tagname) {
            printf("%d %s %s:%d\n", i + 1, tag_stack[i].tagname,
                tag_stack[i].filename, tag_stack[i].line);
        } else {
            printf("%d %s:%d\n", i + 1, tag_stack[i].filename, tag_stack[i].line);
        }
    }
    if (current_tag_name && *current_tag_name) {
        printf("> %s %s:%d\n", current_tag_name, tag_display_filename(b),
            buf_current_line(b));
    } else {
        printf("> %s:%d\n", tag_display_filename(b), buf_current_line(b));
    }
    return 1;
}

/**
 * Move to the address of a tags entry: a line number, or a /pattern/ or
 * ?pattern? search -- and nothing else.  A tags file is data, often from
 * the directory being edited; its address field must never run a command.
 * @return 0, or -1 (reported) if the field is not such an address or names
 *         no line
 */
static int
apply_tag_address(buffer_t *b, char *addr_text)
{
    char *p = addr_text;
    int err = 0;
    int line;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!(isdigit((unsigned char)*p) || *p == '/' || *p == '?')) {
        exvi_report_error("Bad tag address");
        return -1;
    }
    line = parse_address_checked(b, &p, &err);
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (err || *p != '\0' || line < 1 || line > b->line_count) {
        exvi_report_error("Bad tag address");
        return -1;
    }
    b->cur = buf_get_line(b, line);
    /* ex shows the line, as historic ex did running the address as a command */
    if (!visual_mode) {
        print_range(b, line, line, option_number, option_list);
    }
    return 0;
}

/*
 * Tag names compared as taglength has it (POSIX ex): its first taglength
 * characters, in the name typed and in the tags file, or all of them for
 * 0.  ta fooxx did not find foo with taglength=3.
 */
static int
tag_names_match(const char *in_file, const char *typed)
{
    if (option_taglength > 0) {
        return strncmp(in_file, typed, (size_t)option_taglength) == 0;
    }
    return strcmp(in_file, typed) == 0;
}

int
handle_tag_command(buffer_t *b, const char *args, int force)
{
    char *ptr = (char *)args;
    FILE *f;
    char *line = NULL;
    char *tags_path = NULL;
    const char *tags_cursor = NULL;
    int opened = 0;
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

    /* each file of the tags option in turn, until one has the tag */
next_file:
    f = open_tags_file(&tags_cursor, &tags_path);
    if (!f) {
        free(line);
        if (!opened) {
            exvi_report_error("No tags file");
        } else {
            exvi_report_errorf("Tag not found: %s", ptr);
        }
        return 1;
    }
    opened = 1;

    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret - 1] == '\n') {
            line[ret - 1] = '\0';
        }
        char *tname = strtok(line, "\t ");
        char *tfile = strtok(NULL, "\t ");
        char *tcmd = strtok(NULL, "");

        if (tname && tfile && tcmd && tag_names_match(tname, ptr)) {
            char *old_filename;

            if (exvi_restricted_filename_change(b, tfile)) {
                found = 1;
                break;
            }
            if (b->modified && !force) {
                exvi_report_error("No write since last change");
                free(line);
                fclose(f);
                free(tags_path);
                return 1;
            }
            if (push_tag_frame(b) != 0) {
                exvi_report_error("Out of memory");
                free(line);
                fclose(f);
                free(tags_path);
                return 1;
            }
            old_filename = b->filename ? strdup(b->filename) : NULL;
            buf_free(b);
            buf_init(b);
            b->filename = strdup(tfile);
            if (!b->filename) {
                exvi_report_error("Out of memory");
                free(old_filename);
                free(line);
                fclose(f);
                free(tags_path);
                return 1;
            }
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
            apply_tag_address(b, tcmd);
            replace_saved_string(&current_tag_name, ptr);
            found = 1;
            break;
        }
    }
    fclose(f);
    free(tags_path);
    tags_path = NULL;
    if (!found) {
        goto next_file;
    }
    free(line);
    return 1;
}

/* set: see exvi_options.c */

int
exvi_write_allowed(buffer_t *b, const char *filename, int force)
{
    int to_current = !(filename && b->filename && strcmp(filename, b->filename) != 0);

    if (force || !to_current) {
        return 1;
    }
    if (b->read_failed) {
        /* the buffer holds only what could be read: writing it back loses the rest */
        exvi_report_error("File was not read in full (add ! to override)");
        return 0;
    }
    if (option_readonly) {
        exvi_report_error("File is read only (add ! to override)");
        return 0;
    }
    return 1;
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
        if (exvi_restricted_filename_change(b, exvi_current_arg())) {
            return 1;
        }
        load_current_arg_file(b);
        return 1;
    }
    if (ex_arg_idx + 1 >= ex_argc) {
        exvi_report_error("No more files");
        return 1;
    }
    if (exvi_restricted_filename_change(b, ex_args[ex_arg_idx + 1])) {
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
    if (exvi_restricted_filename_change(b, ex_args[ex_arg_idx - 1])) {
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
    if (exvi_restricted_filename_change(b, ex_args[0])) {
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
        if (exvi_write_recover_snapshot(b) != 0) {
            exvi_report_errorf("Could not preserve file in %s", path);
            free(path);
            return 1;
        }
        exvi_note_recover_file(b, b->filename);
        printf("File preserved\n");
        free(path);
    } else {
        exvi_report_error("No modifications or filename to preserve");
    }
    return 1;
}

int
handle_recover_command(buffer_t *b, const char *args, int force)
{
    char *recover_name;
    char *ptr = (char *)args;

    /* Recovering replaces the buffer, as edit does: not over unwritten
     * changes unless forced (POSIX ex: "recover[!]"). */
    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }

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
    if (exvi_restricted_filename_change(b, recover_name)) {
        free(recover_name);
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
    (void)exvi_write_args(b, args, explicit_range, addr1, addr2, force);
    return 1;
}

/**
 * Whether path names b's current file: the same name, or the same file
 * under another name (./f for f, a link).
 */
int
exvi_is_current_file(buffer_t *b, const char *path)
{
    struct stat a;
    struct stat c;

    if (!b->filename) {
        return 0;
    }
    if (strcmp(path, b->filename) == 0) {
        return 1;
    }
    return stat(path, &a) == 0 && stat(b->filename, &c) == 0
        && a.st_dev == c.st_dev && a.st_ino == c.st_ino;
}

/**
 * Carry out a write command's arguments -- `[>>] [file]`, the range if
 * explicit_range -- as `write` does; `wq` and `xit` write the same way.
 * @return 0 if the buffer was written, -1 (reported) if not
 */
int
exvi_write_args(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2, int force)
{
    char *ptr = (char *)args;
    char *target = NULL;
    int append = 0;
    int write_addr1 = 1;
    int write_addr2 = b->line_count;
    int rc;

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
            return -1;
        }
        if (exvi_restricted_filename_change(b, target)) {
            free(target);
            return -1;
        }
        if (!exvi_write_allowed(b, target, force)) {
            free(target);
            return -1;
        }
        if (!force && !append && !option_writeany && !exvi_is_current_file(b, target)
                && access(target, F_OK) == 0) {
            /* POSIX write: an existing file other than the current one needs ! */
            exvi_report_errorf("%s: File exists (add ! to override)", target);
            free(target);
            return -1;
        }
        rc = buf_write_range(b, target, append, write_addr1, write_addr2);
        free(target);
        if (rc != 0) {
            exvi_note_error();
        }
        return rc;
    }
    if (b->filename) {
        if (!exvi_write_allowed(b, b->filename, force)) {
            return -1;
        }
        rc = buf_write_range(b, b->filename, append, write_addr1, write_addr2);
        if (rc != 0) {
            /* (reported with perror and the like, not exvi_report_error) */
            exvi_note_error();
        }
        return rc;
    }
    exvi_report_error("No current filename");
    return -1;
}

int
handle_edit_command(buffer_t *b, const char *args, int force)
{
    char *ptr = (char *)args;
    char *old_filename = NULL;
    char *new_filename = NULL;
    char *plus_cmd = NULL;
    int replace_alt = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    if (b->modified && !force) {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }

    /*
     * edit +command file: the command runs once the file is loaded (POSIX
     * ex), and ends at a blank no backslash escapes.  `e +2 three` edited
     * a file called "+2 three".
     */
    if (*ptr == '+') {
        size_t n = 0;

        ptr++;
        plus_cmd = malloc(strlen(ptr) + 1);
        if (!plus_cmd) {
            perror("malloc");
            return 1;
        }
        while (*ptr && !isspace((unsigned char)*ptr)) {
            if (*ptr == '\\' && ptr[1] != '\0') {
                ptr++;
            }
            plus_cmd[n++] = *ptr++;
        }
        plus_cmd[n] = '\0';
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
    }

    if (b->filename) {
        old_filename = strdup(b->filename);
        if (!old_filename) {
            perror("strdup");
            free(plus_cmd);
            return 1;
        }
    }

    if (!*ptr && old_filename) {
        new_filename = strdup(old_filename);
        if (!new_filename) {
            free(old_filename);
            free(plus_cmd);
            perror("strdup");
            return 1;
        }
    } else if (*ptr) {
        new_filename = expand_filename_refs(b, ptr);
        if (!new_filename) {
            free(old_filename);
            free(plus_cmd);
            return 1;
        }
    }

    if (old_filename && new_filename && strcmp(old_filename, new_filename) != 0) {
        replace_alt = 1;
    }
    if (exvi_restricted_filename_change(b, new_filename)) {
        free(old_filename);
        free(new_filename);
        free(plus_cmd);
        return 1;
    }

    buf_free(b);
    buf_init(b);
    b->filename = new_filename;
    if (replace_alt) {
        replace_saved_string(&alternate_filename, old_filename);
    }
    if (b->filename) {
        buf_read_file(b, b->filename);
        exvi_set_loaded_line(b);
        if (!batch_mode) {
            printf("\"%s\" %d lines\n", b->filename, b->line_count);
        }
        if (plus_cmd) {
            /* `+` alone is the last line, as for the command line's */
            if (*plus_cmd) {
                exvi_execute_command(b, plus_cmd);
            } else if (b->tail) {
                b->cur = b->tail;
            }
        }
    } else {
        exvi_report_error("No current filename");
    }
    free(plus_cmd);
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
        if (secure_mode || restricted_mode) {
            exvi_report_shell_forbidden();
            return 1;
        }
        is_pipe = 1;
        expanded_name = exvi_shell_expand(b, ptr + 1);
        if (!expanded_name) {
            return 1;
        }
        display_name = expanded_name;
        f = exvi_popen(expanded_name, "r");
    } else {
        if (!*ptr) {
            ptr = b->filename;
        }
        if (ptr) {
            expanded_name = expand_filename_refs(b, ptr);
            if (!expanded_name) {
                return 1;
            }
            if (exvi_restricted_filename_change(b, expanded_name)) {
                free(expanded_name);
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
        int last_had_newline = 0;
        char *line = NULL;
        size_t cap = 0;
        ssize_t ret;
        int dest = default_read_destination(b, addr2);
        line_t *pos = buf_get_line(b, dest);
        int inserting_at_eof = (pos == b->tail);

        if (addr2 == -1 && b->empty_origin && b->line_count == 1 &&
            b->head == b->tail && b->head && b->head->len == 0) {
            replace_empty_line = 1;
            pos = NULL;
            inserting_at_eof = 1;
        }

        while ((ret = getline(&line, &cap, f)) != -1) {
            if (ret > 0 && line[ret - 1] == '\n') {
                line[--ret] = '\0';
                last_had_newline = 1;
            } else {
                last_had_newline = 0;
            }
            /* from a file: beautify's; a command's output is no file */
            if (option_beautify && !is_pipe) {
                ret = (ssize_t)exvi_beautify(line, (size_t)ret);
            }
            if (replace_empty_line) {
                /* every byte read, a NUL too */
                char *text = malloc((size_t)ret + 1);

                if (text) {
                    memcpy(text, line, (size_t)ret + 1);
                }
                if (!text) {
                    free(line);
                    if (is_pipe) {
                        exvi_pclose(f);
                    } else {
                        fclose(f);
                    }
                    free(expanded_name);
                    perror("strdup");
                    return 1;
                }
                exvi_note_buffer_change();
                free(b->head->text);
                b->head->text = text;
                b->head->len = (size_t)ret;
                b->cur = b->head;
                b->modified = 1;
                b->empty_origin = 0;
                b->started_empty = 0;
                b->trailing_newline = 1;
                pos = b->head;
                replace_empty_line = 0;
            } else {
                pos = buf_insert_after_n(b, pos, line, (size_t)ret);
            }
            lines_read++;
        }
        free(line);

        if (is_pipe) {
            exvi_pclose(f);
        } else {
            fclose(f);
        }

        if (lines_read > 0 && inserting_at_eof) {
            b->trailing_newline = last_had_newline;
        }
        if (lines_read > 0 && pos) {
            b->cur = pos;           /* POSIX: the last line read */
        }

        if (!batch_mode && display_name) {
            printf("\"%s\" %d lines\n", display_name, lines_read);
        }
        free(expanded_name);
    } else {
        perror(expanded_name ? expanded_name : ptr);
        exvi_note_error();
        free(expanded_name);
    }
    return 1;
}
