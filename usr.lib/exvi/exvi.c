#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <errno.h>

int secure_mode = 0;
static int batch_mode = 0;
static int visual_mode = 0;
static int recover_mode = 0;
static int option_number = 0;
static int option_list = 0;
char *last_search_pattern = NULL;
static char *last_sub_pattern = NULL;
static char *last_sub_replacement = NULL;
static char *alternate_filename = NULL;
static char *visual_handoff_file = NULL;
static int last_sub_global = 0;
static const char *exvi_progname = "ex";

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

static buffer_t regs[27]; // 0-25 = a-z, 26 = unnamed

buffer_t undo_buf;
int undo_valid = 0;

static jmp_buf main_loop_jmp;
static buffer_t *global_buf_for_sighandler = NULL;

static void do_command(buffer_t *b, char *cmd);
void replace_saved_string(char **dst, const char *src);
static void free_ex_arglist(void);
static char *expand_filename_refs(buffer_t *b, const char *arg);
static void free_tag_stack(void);
static char *recover_path_for(const char *filename);
static int load_recover_into_buffer(buffer_t *b, const char *filename);
static void load_startup_commands(buffer_t *b);
static void set_visual_handoff_file(const char *filename);

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

static char *
expand_filename_refs(buffer_t *b, const char *arg)
{
    size_t out_len = 0;
    char *out;
    const char *p;

    if (!arg) {
        return NULL;
    }
    if (arg[0] == '!') {
        return strdup(arg);
    }

    for (p = arg; *p; p++) {
        if (*p == '%') {
            if (!b->filename) {
                fprintf(stderr, "No current filename\n");
                return NULL;
            }
            out_len += strlen(b->filename);
        } else if (*p == '#') {
            if (!alternate_filename) {
                fprintf(stderr, "No alternate filename\n");
                return NULL;
            }
            out_len += strlen(alternate_filename);
        } else {
            out_len++;
        }
    }

    out = malloc(out_len + 1);
    if (!out) {
        return NULL;
    }

    out_len = 0;
    for (p = arg; *p; p++) {
        const char *src = NULL;
        size_t len = 0;

        if (*p == '%') {
            src = b->filename;
            len = strlen(src);
        } else if (*p == '#') {
            src = alternate_filename;
            len = strlen(src);
        } else {
            out[out_len++] = *p;
            continue;
        }

        memcpy(out + out_len, src, len);
        out_len += len;
    }
    out[out_len] = '\0';
    return out;
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

static char *
recover_path_for(const char *filename)
{
    char *path = NULL;

    if (!filename) {
        return NULL;
    }
    if (asprintf(&path, "%s.recover", filename) < 0) {
        return NULL;
    }
    return path;
}

static int
load_recover_into_buffer(buffer_t *b, const char *filename)
{
    char *path;
    struct stat st;

    path = recover_path_for(filename);
    if (!path) {
        return -1;
    }
    if (stat(path, &st) != 0) {
        free(path);
        return -1;
    }

    buf_free(b);
    buf_init(b);
    b->filename = strdup(filename);
    if (!b->filename) {
        free(path);
        return -1;
    }
    buf_read_file(b, path);
    b->modified = 1;
    free(path);
    return 0;
}

static void
load_startup_commands(buffer_t *b)
{
    char *exinit = getenv("EXINIT");

    if (exinit) {
        char *exinit_cpy = strdup(exinit);

        if (exinit_cpy) {
            do_command(b, exinit_cpy);
            free(exinit_cpy);
        }
        return;
    }

    if (!secure_mode) {
        char *home = getenv("HOME");

        if (home) {
            char path[1024];
            struct stat st;

            snprintf(path, sizeof(path), "%s/.exrc", home);
            if (stat(path, &st) == 0
                && S_ISREG(st.st_mode)
                && (st.st_uid == getuid() || st.st_uid == 0)
                && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0) {
                FILE *f = fopen(path, "r");

                if (f) {
                    char *rc_line = NULL;
                    size_t rc_cap = 0;
                    ssize_t rc_ret;

                    while ((rc_ret = getline(&rc_line, &rc_cap, f)) != -1) {
                        if (rc_ret > 0 && rc_line[rc_ret - 1] == '\n') {
                            rc_line[rc_ret - 1] = '\0';
                        }
                        do_command(b, rc_line);
                    }
                    free(rc_line);
                    fclose(f);
                }
            }
        }
    }
}

static void
set_visual_handoff_file(const char *filename)
{
    free(visual_handoff_file);
    visual_handoff_file = filename ? strdup(filename) : NULL;
}

const char *
exvi_handoff_file(void)
{
    return visual_handoff_file;
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

void handle_sigint(int sig) {
    (void)sig;
    printf("\nInterrupt\n");
    longjmp(main_loop_jmp, 1);
}

void handle_sigterm(int sig) {
    (void)sig;
    if (global_buf_for_sighandler && global_buf_for_sighandler->modified && global_buf_for_sighandler->filename && global_buf_for_sighandler->head) {
        char *path = recover_path_for(global_buf_for_sighandler->filename);
        if (path) {
            FILE *f = fopen(path, "w");
            if (f) {
                line_t *curr = global_buf_for_sighandler->head;
                while (curr) {
                    fprintf(f, "%s\n", curr->text);
                    curr = curr->next;
                }
                fclose(f);
            }
            free(path);
        }
    }
    exit(1);
}

static void
set_default_current_range(buffer_t *b, int *addr1, int *addr2)
{
    int cur = buf_current_line(b);

    if (cur == -1) {
        *addr1 = -1;
        *addr2 = -1;
        return;
    }

    *addr1 = cur;
    *addr2 = cur;
}

static int
match_command(const char *cmd, const char *name, const char *abbr, const char **argp,
    int *forcep)
{
    size_t name_len = strlen(name);
    size_t abbr_len = abbr ? strlen(abbr) : 0;
    size_t used = 0;

    if (strncmp(cmd, name, name_len) == 0) {
        used = name_len;
    } else if (abbr && strncmp(cmd, abbr, abbr_len) == 0) {
        used = abbr_len;
    } else {
        return 0;
    }

    if (isalpha((unsigned char)cmd[used])) {
        return 0;
    }

    if (forcep) {
        *forcep = (cmd[used] == '!');
    }
    if (argp) {
        *argp = cmd + used + ((cmd[used] == '!') ? 1 : 0);
    }
    return 1;
}

static int input_mode = 0; // 0=cmd, 1=append, 2=insert, 3=change
static line_t *input_insert_pos = NULL;

static void
print_line_text(line_t *l, int listed)
{
    if (listed) {
        for (size_t j = 0; j < l->len; j++) {
            unsigned char c = l->text[j];

            if (c == '\t') {
                printf("^I");
            } else if (c < 32 || c == 127) {
                printf("^%c", (c == 127) ? '?' : c + 64);
            } else {
                putchar(c);
            }
        }
        printf("$\n");
    } else {
        printf("%s\n", l->text);
    }
}

static void
print_range(buffer_t *b, int addr1, int addr2, int numbered, int listed)
{
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *l = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
            if (numbered) {
                printf("%6d  ", addr1 + i);
            }
            print_line_text(l, listed);
            b->cur = l;
            l = l->next;
        }
    }
}

void
replace_saved_string(char **dst, const char *src)
{
    char *copy = NULL;

    if (src) {
        copy = strdup(src);
    }

    free(*dst);
    *dst = copy;
}

static int
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

static int
handle_tag_command(buffer_t *b, const char *args)
{
    char *ptr = (char *)args;
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    int found = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (!*ptr) {
        fprintf(stderr, "Usage: tag <name>\n");
        return 1;
    }

    f = fopen("tags", "r");
    if (!f) {
        fprintf(stderr, "No tags file\n");
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
                fprintf(stderr, "No write since last change\n");
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
            do_command(b, tcmd);
            found = 1;
            break;
        }
    }
    free(line);
    fclose(f);
    if (!found) {
        fprintf(stderr, "Tag not found: %s\n", ptr);
    }
    return 1;
}

static int
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
        return 1;
    }

    while (*ptr) {
        char *end = ptr;
        size_t len;
        int query = 0;

        while (*end && !isspace((unsigned char)*end)) {
            end++;
        }
        len = (size_t)(end - ptr);
        if (len > 0 && end[-1] == '?') {
            query = 1;
            len--;
        }

        if (len == 3 && strncmp(ptr, "all", 3) == 0) {
            if (query) {
                fprintf(stderr, "Unknown option: %.*s\n", (int)(end - ptr), ptr);
                return 1;
            }
            if (option_number) {
                printf("number\n");
            }
            if (option_list) {
                printf("list\n");
            }
        } else if (len == 2 && strncmp(ptr, "nu", 2) == 0) {
            if (query) {
                printf("%s\n", option_number ? "number" : "nonumber");
            } else {
                option_number = 1;
            }
        } else if (len == 6 && strncmp(ptr, "number", 6) == 0) {
            if (query) {
                printf("%s\n", option_number ? "number" : "nonumber");
            } else {
                option_number = 1;
            }
        } else if (len == 4 && strncmp(ptr, "nonu", 4) == 0) {
            if (query) {
                printf("%s\n", option_number ? "nonumber" : "number");
            } else {
                option_number = 0;
            }
        } else if (len == 8 && strncmp(ptr, "nonumber", 8) == 0) {
            if (query) {
                printf("%s\n", option_number ? "nonumber" : "number");
            } else {
                option_number = 0;
            }
        } else if (len == 2 && strncmp(ptr, "li", 2) == 0) {
            if (query) {
                printf("%s\n", option_list ? "list" : "nolist");
            } else {
                option_list = 1;
            }
        } else if (len == 4 && strncmp(ptr, "list", 4) == 0) {
            if (query) {
                printf("%s\n", option_list ? "list" : "nolist");
            } else {
                option_list = 1;
            }
        } else if (len == 4 && strncmp(ptr, "noli", 4) == 0) {
            if (query) {
                printf("%s\n", option_list ? "nolist" : "list");
            } else {
                option_list = 0;
            }
        } else if (len == 6 && strncmp(ptr, "nolist", 6) == 0) {
            if (query) {
                printf("%s\n", option_list ? "nolist" : "list");
            } else {
                option_list = 0;
            }
        } else {
            fprintf(stderr, "Unknown option: %.*s\n", (int)(end - ptr), ptr);
            return 1;
        }

        ptr = end;
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
    }
    return 1;
}

static int
apply_substitute_range(buffer_t *b, int addr1, int addr2, const char *pattern,
    const char *replacement, int global)
{
    regex_t re;

    if (!pattern || !replacement) {
        return 0;
    }
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
        return 0;
    }

    if (addr1 > 0 && addr2 >= addr1) {
        line_t *l = buf_get_line(b, addr1);
        size_t rep_len = strlen(replacement);

        for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
            regmatch_t pm;
            char *search_start = l->text;
            int matches = 0;
            size_t new_len = 0;
            char *new_text = malloc(1);

            if (!new_text) {
                regfree(&re);
                return 0;
            }
            new_text[0] = '\0';

            while (regexec(&re, search_start, 1, &pm, 0) == 0) {
                char *grown;

                matches++;
                grown = realloc(new_text, new_len + pm.rm_so + rep_len + 1);
                if (!grown) {
                    free(new_text);
                    regfree(&re);
                    return 0;
                }
                new_text = grown;
                memcpy(new_text + new_len, search_start, pm.rm_so);
                new_len += pm.rm_so;
                memcpy(new_text + new_len, replacement, rep_len);
                new_len += rep_len;
                new_text[new_len] = '\0';

                search_start += pm.rm_eo;
                if (!global) {
                    break;
                }
                if (pm.rm_so == pm.rm_eo) {
                    if (*search_start) {
                        grown = realloc(new_text, new_len + 2);
                        if (!grown) {
                            free(new_text);
                            regfree(&re);
                            return 0;
                        }
                        new_text = grown;
                        new_text[new_len++] = *search_start++;
                        new_text[new_len] = '\0';
                    } else {
                        break;
                    }
                }
            }

            if (matches > 0) {
                size_t rem_len = strlen(search_start);
                char *grown = realloc(new_text, new_len + rem_len + 1);

                if (!grown) {
                    free(new_text);
                    regfree(&re);
                    return 0;
                }
                new_text = grown;
                memcpy(new_text + new_len, search_start, rem_len);
                new_len += rem_len;
                new_text[new_len] = '\0';

                free(l->text);
                l->text = new_text;
                l->len = new_len;
                b->modified = 1;
                b->cur = l;
            } else {
                free(new_text);
            }
            l = l->next;
        }
    }

    regfree(&re);
    return 1;
}

static int
default_read_destination(buffer_t *b, int addr2)
{
    if (addr2 != -1) {
        return addr2;
    }
    return b->cur ? buf_current_line(b) : 0;
}

void do_command(buffer_t *b, char *cmd) {
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    
    // Command Separator Handling `|`
    // Note: this simple split will fail on quoted `|`.
    char *pipe = strchr(cmd, '|');
    if (pipe) {
        *pipe = '\0';
        do_command(b, cmd);
        do_command(b, pipe + 1);
        return;
    }
    
    // Comments `"`
    if (*cmd == '"') return;
    
    int addr1, addr2;
    int explicit_range = parse_range(b, &cmd, &addr1, &addr2);
    
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (!*cmd) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr2 > 0) {
            b->cur = buf_get_line(b, addr2);
        }
        print_range(b, addr1, addr2, option_number, option_list);
        return;
    }
    
    // Commands implementation skeleton
    const char *args = NULL;
    int force = 0;

    if (match_command(cmd, "visual", "vi", &args, &force)) {
        if (visual_mode) {
            fprintf(stderr, "%s: visual mode not implemented in this build.\n", exvi_progname);
            return;
        }
        if (b->modified) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        set_visual_handoff_file(b->filename);
        longjmp(main_loop_jmp, EXVI_EXIT_VISUAL_HANDOFF);
    } else if (match_command(cmd, "args", NULL, &args, NULL)) {
        const char *ptr = args;

        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if (*ptr) {
            if (set_ex_arglist_from_words(ptr) != 0) {
                fprintf(stderr, "Usage: args file ...\n");
                return;
            }
        }
        if (ex_argc == 0) {
            printf("No files\n");
            return;
        }
        for (int i = 0; i < ex_argc; i++) {
            if (i == ex_arg_idx) printf("[%s] ", ex_args[i]);
            else printf("%s ", ex_args[i]);
        }
        printf("\n");
        return;
    } else if (match_command(cmd, "next", "n", &args, &force)) {
        int replaced_args = 0;

        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        if (args) {
            const char *ptr = args;

            while (*ptr && isspace((unsigned char)*ptr)) {
                ptr++;
            }
            if (*ptr) {
                if (set_ex_arglist_from_words(ptr) != 0) {
                    fprintf(stderr, "Usage: next [file ...]\n");
                    return;
                }
                replaced_args = 1;
            }
        }
        if (replaced_args) {
            load_current_arg_file(b);
            return;
        }
        if (ex_arg_idx + 1 >= ex_argc) {
            fprintf(stderr, "No more files\n");
            return;
        }
        ex_arg_idx++;
        load_current_arg_file(b);
        return;
    } else if (match_command(cmd, "prev", NULL, &args, &force)) {
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        if (ex_arg_idx - 1 < 0) {
            fprintf(stderr, "No previous files\n");
            return;
        }
        ex_arg_idx--;
        load_current_arg_file(b);
        return;
    } else if (match_command(cmd, "rewind", "rew", &args, &force)) {
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        if (ex_argc == 0) {
            fprintf(stderr, "No files\n");
            return;
        }
        ex_arg_idx = 0;
        load_current_arg_file(b);
        return;
    } else if (match_command(cmd, "preserve", "pre", &args, NULL)) {
        if (b->filename && b->modified && b->head) {
            char *path = recover_path_for(b->filename);

            if (!path) {
                fprintf(stderr, "Out of memory\n");
                return;
            }
            buf_write_file(b, path, 0);
            printf("File preserved as %s\n", path);
            free(path);
        } else {
            fprintf(stderr, "No modifications or filename to preserve\n");
        }
        return;
    } else if (match_command(cmd, "recover", "rec", &args, NULL)) {
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
            recover_name = NULL;
        }

        if (!recover_name) {
            fprintf(stderr, "No current filename\n");
            return;
        }
        if (load_recover_into_buffer(b, recover_name) != 0) {
            fprintf(stderr, "No recover file for %s\n", recover_name);
            free(recover_name);
            return;
        }
        free(recover_name);
        printf("\"%s\" recovered, %d lines\n", b->filename, b->line_count);
        return;
    } else if (match_command(cmd, "pop", "po", &args, &force)) {
        if (handle_pop_command(b, force)) {
            return;
        }
    } else if (match_command(cmd, "tag", NULL, &args, NULL)) {
        if (handle_tag_command(b, args)) {
            return;
        }
    } else if (match_command(cmd, "set", NULL, &args, NULL)) {
        if (handle_set_command(args)) {
            return;
        }
    } else if (match_command(cmd, "quit", "q", &args, &force)) {
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        exit(0);
    } else if (match_command(cmd, "xit", "x", &args, &force)
        || match_command(cmd, "wq", NULL, &args, &force)) {
        if (!b->filename) {
            fprintf(stderr, "No current filename\n");
            return;
        }
        buf_write_file(b, b->filename, 0);
        if (!b->modified || force) {
            exit(0);
        }
    } else if (match_command(cmd, "write", "w", &args, NULL)) {
        char *ptr = (char *)args;
        char *target = NULL;
        int append = 0;
        int write_addr1 = 1;
        int write_addr2 = b->line_count;

        if (explicit_range) {
            write_addr1 = addr1;
            write_addr2 = addr2;
        }
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '>' && *(ptr+1) == '>') {
            append = 1;
            ptr += 2;
            while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        }
        if (*ptr) {
            target = expand_filename_refs(b, ptr);
            if (!target) {
                return;
            }
            buf_write_range(b, target, append, write_addr1, write_addr2);
            free(target);
        } else if (b->filename) {
            buf_write_range(b, b->filename, append, write_addr1, write_addr2);
        } else {
            fprintf(stderr, "No current filename\n");
        }
    } else if (match_command(cmd, "edit", "e", &args, &force)) {
        char *ptr = (char *)args;
        char *old_filename = NULL;
        char *new_filename = NULL;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }

        if (!*ptr && b->filename) {
            old_filename = strdup(b->filename);
            if (!old_filename) {
                perror("strdup");
                return;
            }
        } else if (*ptr) {
            new_filename = expand_filename_refs(b, ptr);
            if (!new_filename) {
                return;
            }
        }
        
        buf_free(b);
        buf_init(b);
        if (*ptr) {
            b->filename = new_filename;
        } else if (old_filename) {
            b->filename = old_filename;
            old_filename = NULL;
        }
        replace_saved_string(&alternate_filename, old_filename);
        if (b->filename) {
            buf_read_file(b, b->filename);
            if (!batch_mode) printf("\"%s\" %d lines\n", b->filename, b->line_count);
        } else {
            fprintf(stderr, "No current filename\n");
        }
        free(old_filename);
    } else if (match_command(cmd, "read", "r", &args, NULL)) {
        char *ptr = (char *)args;
        const char *display_name;
        char *expanded_name = NULL;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        
        FILE *f = NULL;
        int is_pipe = 0;
        if (*ptr == '!') {
            if (secure_mode) {
                fprintf(stderr, "Shell commands not allowed in secure mode\n");
                return;
            }
            is_pipe = 1;
            display_name = ptr + 1;
            f = popen(ptr + 1, "r");
        } else {
            if (!*ptr) ptr = b->filename; // default to current file
            if (ptr) {
                expanded_name = expand_filename_refs(b, ptr);
                if (!expanded_name) {
                    return;
                }
            }
            display_name = expanded_name;
            if (expanded_name) f = fopen(expanded_name, "r");
        }
        
        if (f) {
            int lines_read = 0;
            char *line = NULL;
            size_t cap = 0;
            ssize_t ret;
            int dest = default_read_destination(b, addr2);
            line_t *pos = buf_get_line(b, dest);
            
            while ((ret = getline(&line, &cap, f)) != -1) {
                if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
                pos = buf_insert_after(b, pos, line);
                lines_read++;
            }
            free(line);
            
            if (is_pipe) pclose(f);
            else fclose(f);
            
            if (!batch_mode && display_name) {
                printf("\"%s\" %d lines\n", display_name, lines_read);
            }
            free(expanded_name);
        } else {
            perror(expanded_name ? expanded_name : ptr);
            free(expanded_name);
        }
    } else if (match_command(cmd, "delete", "d", &args, NULL)) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            for (int i = 0; i < (addr2 - addr1 + 1); i++) {
                line_t *l = buf_get_line(b, addr1);
                if (l) buf_delete(b, l);
            }
        }
    } else if (match_command(cmd, "undo", "u", &args, NULL)) {
        if (undo_valid) {
            buffer_t tmp;
            buf_init(&tmp);
            buf_copy(&tmp, b);
            buf_copy(b, &undo_buf);
            buf_copy(&undo_buf, &tmp);
            buf_free(&tmp);
            undo_valid = 1; // tmp doesn't survive? Yes, but undo_buf holds the new undo state
        }
    } else if (match_command(cmd, "put", "pu", &args, NULL)) {
        int reg_idx = 26;
        char *ptr = (char *)args;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr >= 'a' && *ptr <= 'z') reg_idx = *ptr - 'a';
        
        save_undo(b);
        line_t *pos = buf_get_line(b, addr2 != -1 ? addr2 : (b->cur ? buf_current_line(b) : 0));
        line_t *src = regs[reg_idx].head;
        while (src) {
            pos = buf_insert_after(b, pos, src->text);
            src = src->next;
        }
    } else if (match_command(cmd, "print", "p", &args, NULL)
        || match_command(cmd, "number", "#", &args, NULL)
        || match_command(cmd, "list", "l", &args, NULL)) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            line_t *l = buf_get_line(b, addr1);
            int numbered = (cmd[0] == '#' || strncmp(cmd, "number", 6) == 0
                || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_number));
            int listed = (cmd[0] == 'l' || strncmp(cmd, "list", 4) == 0
                || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_list));
            for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
                if (numbered) {
                    printf("%6d  ", addr1 + i);
                }
                print_line_text(l, listed);
                b->cur = l;
                l = l->next;
            }
        }
    } else if (cmd[0] == '=') {
        int target = (addr2 != -1) ? addr2 : b->line_count;
        printf("%d\n", target);
    } else if (cmd[0] == 'k' || match_command(cmd, "mark", NULL, &args, NULL)) {
        char *ptr = (cmd[0] == 'k') ? cmd + 1 : (char *)args;
        int target;

        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if (*ptr < 'a' || *ptr > 'z') {
            fprintf(stderr, "Usage: mark <a-z>\n");
            return;
        }
        target = (addr2 != -1) ? addr2 : buf_current_line(b);
        if (target < 1) {
            fprintf(stderr, "No current line\n");
            return;
        }
        b->marks[*ptr - 'a'] = buf_get_line(b, target);
    } else if (match_command(cmd, "file", "f", &args, NULL)) {
        char *ptr = (char *)args;
        char *new_name = NULL;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr) {
            new_name = expand_filename_refs(b, ptr);
            if (!new_name) {
                return;
            }
            replace_saved_string(&alternate_filename, b->filename);
            if (b->filename) free(b->filename);
            b->filename = new_name;
        }
        printf("\"%s\" %s %d lines\n", b->filename ? b->filename : "No File", b->modified ? "[Modified]" : "", b->line_count);
    } else if (match_command(cmd, "append", "a", &args, NULL)) {
        save_undo(b);
        input_mode = 1; // append
        input_insert_pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
    } else if (match_command(cmd, "insert", "i", &args, NULL)) {
        save_undo(b);
        input_mode = 2; // insert
        line_t *pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
        input_insert_pos = pos ? pos->prev : NULL;
    } else if (match_command(cmd, "change", "c", &args, NULL)) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            for (int i = 0; i < (addr2 - addr1 + 1); i++) {
                line_t *l = buf_get_line(b, addr1);
                if (l) buf_delete(b, l);
            }
        }
        input_mode = 3; // change
        input_insert_pos = (addr1 > 1) ? buf_get_line(b, addr1 - 1) : NULL;
    } else if (match_command(cmd, "copy", "co", &args, NULL)
        || match_command(cmd, "copy", "t", &args, NULL)) {
        char *ptr = (char *)args;
        int dest = parse_address(b, &ptr);

        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (dest == -1) {
            fprintf(stderr, "Destination required\n");
            return;
        }

        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            line_t *pos = buf_get_line(b, dest); // can be NULL if dest=0
            line_t *src = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
                pos = buf_insert_after(b, pos, src->text);
                src = src->next;
            }
        }
    } else if (match_command(cmd, "move", "m", &args, NULL)) {
        char *ptr = (char *)args;
        int dest = parse_address(b, &ptr);

        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (dest == -1) {
            fprintf(stderr, "Destination required\n");
            return;
        }
        if (addr1 != -1 && addr2 != -1 && dest >= addr1 && dest <= addr2) {
            fprintf(stderr, "Destination not outside move range\n");
            return;
        }

        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            // Move: Extract the nodes and place them after dest.
            // Simple approach: copy then delete original. Must carefully handle dest shift if dest > addr2
            line_t *pos = buf_get_line(b, dest);
            line_t *src = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
                pos = buf_insert_after(b, pos, src->text);
                src = src->next;
            }
            // Delete original
            // Note: if dest was before addr1, addr1 shifted down by (addr2-addr1+1).
            int del_start = addr1;
            if (dest < addr1) {
                del_start += (addr2 - addr1 + 1);
            }
            for (int i = 0; i < (addr2 - addr1 + 1); i++) {
                line_t *l = buf_get_line(b, del_start);
                if (l) buf_delete(b, l);
            }
        }
    } else if (match_command(cmd, "join", "j", &args, NULL)) {
        if (!explicit_range) {
            int cur = buf_current_line(b);
            addr1 = cur;
            addr2 = (cur != -1) ? cur + 1 : -1;
        }
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 < addr2) {
            line_t *first = buf_get_line(b, addr1);
            if (!first) return;
            // join lines addr1 through addr2
            size_t total_len = first->len;
            line_t *nxt_len = first->next;
            for (int i = 1; i <= (addr2 - addr1) && nxt_len; i++) {
                total_len += nxt_len->len;
                nxt_len = nxt_len->next;
            }
            char *joined = malloc(total_len + (addr2 - addr1 + 1));
            size_t cur_len = first->len;
            memcpy(joined, first->text, cur_len);
            joined[cur_len] = '\0';
            line_t *nxt = first->next;
            for (int i = 1; i <= (addr2 - addr1) && nxt; i++) {
                line_t *to_delete = nxt;
                nxt = nxt->next;
                // Add a space if the previous string doesn't end with a space and the next doesn't start with one
                if (cur_len > 0 && joined[cur_len-1] != ' ' && joined[cur_len-1] != '\t' && to_delete->text[0] != ' ' && to_delete->text[0] != '\t' && to_delete->text[0] != ')') {
                    joined[cur_len] = ' ';
                    cur_len++;
                }
                memcpy(joined + cur_len, to_delete->text, to_delete->len);
                cur_len += to_delete->len;
                joined[cur_len] = '\0';
                buf_delete(b, to_delete);
            }
            free(first->text);
            first->text = joined;
            first->len = cur_len;
        }
    } else if (match_command(cmd, "yank", "y", &args, NULL)) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        int reg_idx = 26; // unnamed
        char *ptr = (char *)args;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr >= 'a' && *ptr <= 'z') reg_idx = *ptr - 'a';
        
        buf_free(&regs[reg_idx]);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            line_t *src = buf_get_line(b, addr1);
            line_t *pos = NULL;
            for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
                pos = buf_insert_after(&regs[reg_idx], pos, src->text);
                src = src->next;
            }
        }
    } else if (match_command(cmd, "substitute", "s", &args, NULL)) {
        char *ptr = (char *)args;
        save_undo(b);
        char delim;
        char *spec;
        char *pat_raw;
        char *repl_str;
        char *re_str;
        int global = 0;

        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        delim = *ptr;
        if (!delim || delim == '\n') return;

        spec = ptr + 1;
        pat_raw = parse_delimited_text(&spec, delim);
        if (!pat_raw) {
            return;
        }
        repl_str = parse_delimited_text(&spec, delim);
        if (!repl_str) {
            free(pat_raw);
            return;
        }
        if (strchr(spec, 'g')) {
            global = 1;
        }

        if (*pat_raw == '\0') {
            if (!last_sub_pattern) {
                free(pat_raw);
                free(repl_str);
                return;
            }
            re_str = strdup(last_sub_pattern);
        } else {
            re_str = strdup(pat_raw);
        }
        free(pat_raw);
        if (!re_str) {
            free(repl_str);
            return;
        }

        // Apply to range
        if (addr1 == -1) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr1 > 0 && addr2 >= addr1
            && apply_substitute_range(b, addr1, addr2, re_str, repl_str, global)) {
            replace_saved_string(&last_search_pattern, re_str);
            replace_saved_string(&last_sub_pattern, re_str);
            replace_saved_string(&last_sub_replacement, repl_str);
            last_sub_global = global;
        }
        free(re_str);
        free(repl_str);
    } else if (cmd[0] == '&' && cmd[1] == '\0') {
        if (!last_sub_pattern || !last_sub_replacement) {
            return;
        }
        save_undo(b);
        if (addr1 == -1) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr1 > 0 && addr2 >= addr1) {
            apply_substitute_range(b, addr1, addr2, last_sub_pattern,
                last_sub_replacement, last_sub_global);
        }
    } else if (match_command(cmd, "global", "g", &args, NULL)
        || match_command(cmd, "global", "v", &args, NULL)) {
        char *ptr = (char *)args;
        int inverted = (cmd[0] == 'v');
        char delim;

        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        delim = *ptr;
        if (!delim || delim == '\n') return;
        
        char *re_str = strdup(ptr + 1);
        char *end_re = strchr(re_str, delim);
        if (!end_re) { free(re_str); return; }
        *end_re = '\0';
        
        char *exec_cmd = end_re + 1;
        while (*exec_cmd && isspace((unsigned char)*exec_cmd)) exec_cmd++;
        if (!*exec_cmd) exec_cmd = "p"; // default is to print
        
        regex_t re;
        if (regcomp(&re, re_str, REG_EXTENDED) != 0) {
            free(re_str);
            return;
        }
        
        // global commands apply to the whole file by default if no range given
        if (!explicit_range) { addr1 = 1; addr2 = b->line_count; }
        
        // Mark pass
        if (addr1 > 0 && addr2 >= addr1) {
            line_t *l = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
                regmatch_t pm;
                int match = (regexec(&re, l->text, 1, &pm, 0) == 0);
                if ((match && !inverted) || (!match && inverted)) {
                    l->global_mark = 1;
                } else {
                    l->global_mark = 0;
                }
                l = l->next;
            }
        }
        regfree(&re);
        
        // Execution pass
        line_t *curr = b->head;
        while (curr) {
            line_t *next = curr->next;
            if (curr->global_mark) {
                curr->global_mark = 0;
                b->cur = curr;
                char *cmd_cpy = strdup(exec_cmd);
                do_command(b, cmd_cpy);
                free(cmd_cpy);
            }
            curr = next;
        }
        
        free(re_str);
    } else if (cmd[0] == '!') {
        if (secure_mode) {
            fprintf(stderr, "Shell commands not allowed in secure mode\n");
            return;
        }
        char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
        } else if (pid == 0) {
            if (setgid(getgid()) == -1) {
                perror("setgid");
                exit(127);
            }
            if (setuid(getuid()) == -1) {
                perror("setuid");
                exit(127);
            }
            execl(shell, shell, "-c", cmd + 1, (char *)NULL);
            perror("execl");
            exit(127);
        } else {
            int status;
            void (*old_int)(int) = signal(SIGINT, SIG_IGN);
            void (*old_quit)(int) = signal(SIGQUIT, SIG_IGN);
            while (waitpid(pid, &status, 0) == -1) {
                if (errno != EINTR) break;
            }
            signal(SIGINT, old_int);
            signal(SIGQUIT, old_quit);
        }
    }
}

int
exvi_main(int argc, char **argv, exvi_frontend_t frontend)
{
    int opt;
    int interactive_prompt;
    int jump_status;

    secure_mode = 0;
    batch_mode = 0;
    visual_mode = (frontend == EXVI_FRONTEND_VI);
    recover_mode = 0;
    option_number = 0;
    option_list = 0;
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    set_visual_handoff_file(NULL);
    exvi_progname = (frontend == EXVI_FRONTEND_VI) ? "vi" : "ex";
    free_ex_arglist();
    input_mode = 0;
    input_insert_pos = NULL;
    while ((opt = getopt(argc, argv, "sSvr")) != -1) {
        switch (opt) {
        case 's':
            batch_mode = 1;
            break;
        case 'S':
            secure_mode = 1;
            break;
        case 'v':
            visual_mode = 1;
            break;
        case 'r':
            recover_mode = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-s] [-S] [-v] [-r] [file ...]\n", argv[0]);
            exit(1);
        }
    }

    buffer_t buf;
    buf_init(&buf);
    undo_valid = 0;
    buf_init(&undo_buf);
    for (int i=0; i<27; i++) buf_init(&regs[i]);

    if (optind < argc) {
        ex_args = argv + optind;
        ex_argc = argc - optind;
        ex_arg_idx = 0;
        ex_args_owned = 0;
        buf.filename = strdup(ex_args[ex_arg_idx]);
        if (recover_mode) {
            if (load_recover_into_buffer(&buf, buf.filename) != 0) {
                fprintf(stderr, "%s: no recover file for %s\n", exvi_progname, buf.filename);
                buf_free(&buf);
                buf_free(&undo_buf);
                for (int i = 0; i < 27; i++) {
                    buf_free(&regs[i]);
                }
                return 1;
            }
        } else {
            buf_read_file(&buf, buf.filename);
        }
    }
    
    if (visual_mode) {
        if (frontend == EXVI_FRONTEND_EX) {
            set_visual_handoff_file(buf.filename);
            buf_free(&buf);
            buf_free(&undo_buf);
            for (int i = 0; i < 27; i++) {
                buf_free(&regs[i]);
            }
            return EXVI_EXIT_VISUAL_HANDOFF;
        }
        fprintf(stderr, "%s: visual mode not implemented in this build.\n", exvi_progname);
        buf_free(&buf);
        buf_free(&undo_buf);
        for (int i = 0; i < 27; i++) {
            buf_free(&regs[i]);
        }
        return 1;
    }

    load_startup_commands(&buf);

    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    
    global_buf_for_sighandler = &buf;
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigterm);
    signal(SIGTERM, handle_sigterm);
    interactive_prompt = !batch_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    jump_status = setjmp(main_loop_jmp);
    if (jump_status == EXVI_EXIT_VISUAL_HANDOFF) {
        free(line);
        buf_free(&buf);
        buf_free(&undo_buf);
        for (int i = 0; i < 27; i++) {
            buf_free(&regs[i]);
        }
        free(last_search_pattern);
        last_search_pattern = NULL;
        free(last_sub_pattern);
        last_sub_pattern = NULL;
        free(last_sub_replacement);
        last_sub_replacement = NULL;
        last_sub_global = 0;
        free(alternate_filename);
        alternate_filename = NULL;
        free_ex_arglist();
        free_tag_stack();
        return EXVI_EXIT_VISUAL_HANDOFF;
    }

    for (;;) {
        if (interactive_prompt && !input_mode) {
            fputs(":", stdout);
            fflush(stdout);
        }

        ret = getline(&line, &cap, stdin);
        if (ret == -1) {
            break;
        }
        if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
        
        if (input_mode) {
            if (strcmp(line, ".") == 0) {
                input_mode = 0;
            } else {
                input_insert_pos = buf_insert_after(&buf, input_insert_pos, line);
                buf.cur = input_insert_pos;
            }
            continue;
        }

        // Strip optional `:` prefix
        char *cmd_line = line;
        while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
        if (*cmd_line == ':') cmd_line++;
        
        do_command(&buf, cmd_line);
    }
    free(line);
    
    buf_free(&buf);
    buf_free(&undo_buf);
    for (int i=0; i<27; i++) buf_free(&regs[i]);
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    free(alternate_filename);
    alternate_filename = NULL;
    free_ex_arglist();
    free_tag_stack();
    return 0;
}
