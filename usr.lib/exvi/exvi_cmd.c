#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define EXVI_SUB_PRINT_PLAIN  0x01
#define EXVI_SUB_PRINT_NUMBER 0x02
#define EXVI_SUB_PRINT_LIST   0x04

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
        size_t col = 0;

        for (size_t j = 0; j < l->len; j++) {
            unsigned char c = l->text[j];

            if (c == '\t') {
                int tabstop = option_tabstop > 0 ? option_tabstop : 8;
                int spaces = tabstop - (int)(col % (size_t)tabstop);

                while (spaces-- > 0) {
                    putchar(' ');
                    col++;
                }
            } else {
                putchar(c);
                col++;
            }
        }
        putchar('\n');
    }
}

static void
print_line_number_prefix(int line_no)
{
    /*
     * BSD/vim-style numbered print output uses a seven-column field plus a
     * separating space, rather than the looser ad hoc spacing we had before.
     */
    printf("%7d ", line_no);
}

void
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

void
print_range(buffer_t *b, int addr1, int addr2, int numbered, int listed)
{
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *l = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
            if (numbered) {
                print_line_number_prefix(addr1 + i);
            }
            print_line_text(l, listed);
            b->cur = l;
            l = l->next;
        }
    }
}

int
default_read_destination(buffer_t *b, int addr2)
{
    if (addr2 != -1) {
        return addr2;
    }
    return b->cur ? buf_current_line(b) : 0;
}

static int
apply_substitute_range(buffer_t *b, int addr1, int addr2, const char *pattern,
    const char *replacement, int global, int print_flags)
{
    regex_t re;

    if (!pattern || !replacement) {
        return 0;
    }
    if (regcomp(&re, pattern, exvi_regex_flags()) != 0) {
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

                exvi_note_buffer_change();
                free(l->text);
                l->text = new_text;
                l->len = new_len;
                b->modified = 1;
                b->cur = l;
                if (print_flags & EXVI_SUB_PRINT_NUMBER) {
                    print_line_number_prefix(addr1 + i);
                }
                if (print_flags & EXVI_SUB_PRINT_LIST) {
                    print_line_text(l, 1);
                } else if ((print_flags & EXVI_SUB_PRINT_PLAIN)
                    || (print_flags & EXVI_SUB_PRINT_NUMBER)) {
                    print_line_text(l, 0);
                }
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
parse_substitute_flags(const char *spec, int *global, int *print_mode)
{
    while (*spec) {
        if (*spec == 'g') {
            *global = 1;
        } else if (*spec == 'p') {
            *print_mode |= EXVI_SUB_PRINT_PLAIN;
        } else if (*spec == '#') {
            *print_mode |= EXVI_SUB_PRINT_NUMBER;
        } else if (*spec == 'l') {
            *print_mode |= EXVI_SUB_PRINT_LIST;
        } else if (!isspace((unsigned char)*spec)) {
            exvi_report_error("Bad substitute flags");
            return 0;
        }
        spec++;
    }
    return 1;
}

static char *
parse_delimited_text_or_eol(char **cmd_ptr, char delim)
{
    char *src = *cmd_ptr;
    size_t len = 0;
    char *text = malloc(strlen(src) + 1);

    if (!text) {
        return NULL;
    }

    while (*src) {
        if (*src == '\\' && src[1] != '\0') {
            text[len++] = src[1];
            src += 2;
            continue;
        }
        if (*src == delim) {
            text[len] = '\0';
            *cmd_ptr = src + 1;
            return text;
        }
        text[len++] = *src++;
    }

    text[len] = '\0';
    *cmd_ptr = src;
    return text;
}

int
handle_delete_command(buffer_t *b, int explicit_range, int addr1, int addr2)
{
    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    handle_yank_command(b, "", 1, addr1, addr2);
    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, addr1);

            if (l) {
                buf_delete(b, l);
            }
        }
    }
    return 1;
}

int
handle_undo_command(buffer_t *b)
{
    buffer_t snapshot;

    exvi_discard_pending_undo();
    if (undo_history.len > 0) {
        if (exvi_history_push_snapshot(&redo_history, b) != 0) {
            exvi_report_error("out of memory");
            return 1;
        }
        if (exvi_history_pop_snapshot(&undo_history, &snapshot) == 0) {
            exvi_history_suspended++;
            buf_free(b);
            *b = snapshot;
            exvi_history_suspended--;
        }
    }
    return 1;
}

int
handle_redo_command(buffer_t *b)
{
    buffer_t snapshot;

    exvi_discard_pending_undo();
    if (redo_history.len > 0) {
        if (exvi_history_push_snapshot(&undo_history, b) != 0) {
            exvi_report_error("out of memory");
            return 1;
        }
        if (exvi_history_pop_snapshot(&redo_history, &snapshot) == 0) {
            exvi_history_suspended++;
            buf_free(b);
            *b = snapshot;
            exvi_history_suspended--;
        }
    }
    return 1;
}

int
handle_put_command(buffer_t *b, const char *args, int addr2)
{
    int reg_idx = 26;
    char *ptr = (char *)args;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr >= 'a' && *ptr <= 'z') {
        reg_idx = *ptr - 'a';
    }

    save_undo(b);
    line_t *pos = buf_get_line(b, addr2 != -1 ? addr2 :
        (b->cur ? buf_current_line(b) : 0));
    line_t *src = regs[reg_idx].head;

    while (src) {
        pos = buf_insert_after(b, pos, src->text);
        src = src->next;
    }
    return 1;
}

int
handle_print_command(buffer_t *b, const char *cmd, int explicit_range, int addr1,
    int addr2)
{
    int numbered;
    int listed;

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (addr1 < 1 || addr2 < 1) {
        exvi_report_error("No current line");
        return 1;
    }
    numbered = (cmd[0] == '#' || strncmp(cmd, "number", 6) == 0
        || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_number));
    listed = (cmd[0] == 'l' || strncmp(cmd, "list", 4) == 0
        || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_list));
    print_range(b, addr1, addr2, numbered, listed);
    return 1;
}

int
handle_equal_command(buffer_t *b, int explicit_range, int addr2)
{
    int target;

    target = explicit_range ? addr2 : b->line_count;
    printf("%d\n", target);
    return 1;
}

int
handle_mark_command(buffer_t *b, const char *cmd, const char *args, int addr2)
{
    char *ptr = (cmd[0] == 'k') ? (char *)cmd + 1 : (char *)args;
    int target;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr < 'a' || *ptr > 'z') {
        exvi_report_error("Usage: mark <a-z>");
        return 1;
    }
    target = (addr2 != -1) ? addr2 : buf_current_line(b);
    if (target < 1) {
        exvi_report_error("No current line");
        return 1;
    }
    b->marks[*ptr - 'a'] = buf_get_line(b, target);
    b->mark_cols[*ptr - 'a'] = 0;
    return 1;
}

int
handle_file_command(buffer_t *b, const char *args)
{
    char *ptr = (char *)args;
    char *new_name = NULL;
    int replace_alt = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr) {
        new_name = expand_filename_refs(b, ptr);
        if (!new_name) {
            return 1;
        }
        if (exvi_restricted_filename_change(b, new_name)) {
            free(new_name);
            return 1;
        }
        if (b->filename && strcmp(b->filename, new_name) != 0) {
            replace_alt = 1;
        }
        if (replace_alt) {
            replace_saved_string(&alternate_filename, b->filename);
        }
        exvi_retarget_recover_file(b, new_name);
        free(b->filename);
        b->filename = new_name;
    }
    printf("\"%s\"%s%s %d lines\n", b->filename ? b->filename : "No File",
        b->modified ? " [Modified]" : "",
        option_readonly ? " [Readonly]" : "",
        b->line_count);
    return 1;
}

int
handle_input_command(buffer_t *b, int mode, int explicit_range, int addr1, int addr2)
{
    save_undo(b);

    if (mode == 1) {
        input_mode = 1;
        input_insert_pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
        return 1;
    }

    if (mode == 2) {
        line_t *pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;

        input_mode = 2;
        input_insert_pos = pos ? pos->prev : NULL;
        return 1;
    }

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    handle_yank_command(b, "", 1, addr1, addr2);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, addr1);

            if (l) {
                buf_delete(b, l);
            }
        }
    }
    input_mode = 3;
    input_insert_pos = (addr1 > 1) ? buf_get_line(b, addr1 - 1) : NULL;
    return 1;
}

int
handle_copy_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    char *ptr = (char *)args;
    int parse_error = 0;
    int dest = parse_address_checked(b, &ptr, &parse_error);

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (parse_error) {
        exvi_report_error("Bad address");
        return 1;
    }
    if (dest == -1) {
        exvi_report_error("Destination required");
        return 1;
    }

    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *pos = buf_get_line(b, dest);
        line_t *src = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
            pos = buf_insert_after(b, pos, src->text);
            src = src->next;
        }
    }
    return 1;
}

int
handle_move_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    char *ptr = (char *)args;
    int parse_error = 0;
    int dest = parse_address_checked(b, &ptr, &parse_error);

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (parse_error) {
        exvi_report_error("Bad address");
        return 1;
    }
    if (dest == -1) {
        exvi_report_error("Destination required");
        return 1;
    }
    if (addr1 != -1 && addr2 != -1 && dest >= addr1 && dest <= addr2) {
        exvi_report_error("Destination not outside move range");
        return 1;
    }

    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *pos = buf_get_line(b, dest);
        line_t *src = buf_get_line(b, addr1);
        int del_start = addr1;

        for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
            pos = buf_insert_after(b, pos, src->text);
            src = src->next;
        }
        if (dest < addr1) {
            del_start += (addr2 - addr1 + 1);
        }
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, del_start);

            if (l) {
                buf_delete(b, l);
            }
        }
    }
    return 1;
}

int
handle_join_command(buffer_t *b, int explicit_range, int addr1, int addr2)
{
    if (!explicit_range) {
        int cur = buf_current_line(b);

        addr1 = cur;
        addr2 = (cur != -1) ? cur + 1 : -1;
    } else if (addr1 == addr2 && addr1 != -1) {
        addr2 = addr1 + 1;
    }
    if (addr2 > b->line_count) {
        addr2 = b->line_count;
    }
    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 < addr2) {
        line_t *first = buf_get_line(b, addr1);
        line_t *nxt_len;
        char *joined;
        size_t total_len;
        size_t cur_len;
        line_t *nxt;

        if (!first) {
            return 1;
        }
        total_len = first->len;
        nxt_len = first->next;
        for (int i = 1; i <= (addr2 - addr1) && nxt_len; i++) {
            total_len += nxt_len->len;
            nxt_len = nxt_len->next;
        }
        joined = malloc(total_len + (size_t)(addr2 - addr1 + 1));
        if (!joined) {
            return 1;
        }
        cur_len = first->len;
        memcpy(joined, first->text, cur_len);
        joined[cur_len] = '\0';
        nxt = first->next;
        for (int i = 1; i <= (addr2 - addr1) && nxt; i++) {
            line_t *to_delete = nxt;

            nxt = nxt->next;
            if (cur_len > 0 && joined[cur_len - 1] != ' '
                && joined[cur_len - 1] != '\t' && to_delete->text[0] != ' '
                && to_delete->text[0] != '\t' && to_delete->text[0] != ')') {
                joined[cur_len++] = ' ';
            }
            memcpy(joined + cur_len, to_delete->text, to_delete->len);
            cur_len += to_delete->len;
            joined[cur_len] = '\0';
            buf_delete(b, to_delete);
        }
        exvi_note_buffer_change();
        free(first->text);
        first->text = joined;
        first->len = cur_len;
    }
    return 1;
}

int
handle_yank_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    int reg_idx = 26;
    char *ptr = (char *)args;

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr >= 'a' && *ptr <= 'z') {
        reg_idx = *ptr - 'a';
    }

    buf_free(&regs[reg_idx]);
    reg_linewise[reg_idx] = 1;
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *src = buf_get_line(b, addr1);
        line_t *pos = NULL;

        for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
            pos = buf_insert_after(&regs[reg_idx], pos, src->text);
            src = src->next;
        }
    }
    return 1;
}

int
handle_substitute_command(buffer_t *b, const char *args, int addr1, int addr2)
{
    char *ptr = (char *)args;
    char delim;
    char *spec;
    char *pat_raw;
    char *repl_str;
    char *re_str;
    int global = 0;
    int print_mode = 0;

    save_undo(b);
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    delim = *ptr;
    if (!delim || delim == '\n') {
        if (!last_sub_pattern || !last_sub_replacement) {
            return 1;
        }
        if (addr1 == -1) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr1 > 0 && addr2 >= addr1
            && apply_substitute_range(b, addr1, addr2, last_sub_pattern,
                last_sub_replacement, last_sub_global, 0)) {
            replace_saved_string(&last_search_pattern, last_sub_pattern);
        }
        return 1;
    }

    spec = ptr + 1;
    pat_raw = parse_delimited_text(&spec, delim);
    if (!pat_raw) {
        return 1;
    }
    repl_str = parse_delimited_text_or_eol(&spec, delim);
    if (!repl_str) {
        free(pat_raw);
        return 1;
    }
    if (!parse_substitute_flags(spec, &global, &print_mode)) {
        free(pat_raw);
        free(repl_str);
        return 1;
    }

    if (*pat_raw == '\0') {
        if (!last_sub_pattern) {
            free(pat_raw);
            free(repl_str);
            return 1;
        }
        re_str = strdup(last_sub_pattern);
    } else {
        re_str = strdup(pat_raw);
    }
    free(pat_raw);
    if (!re_str) {
        free(repl_str);
        return 1;
    }

    if (addr1 == -1) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (addr1 > 0 && addr2 >= addr1
        && apply_substitute_range(b, addr1, addr2, re_str, repl_str, global,
            print_mode)) {
        replace_saved_string(&last_search_pattern, re_str);
        replace_saved_string(&last_sub_pattern, re_str);
        replace_saved_string(&last_sub_replacement, repl_str);
        last_sub_global = global;
    }
    free(re_str);
    free(repl_str);
    return 1;
}

int
handle_repeat_substitute_command(buffer_t *b, const char *args, int addr1,
    int addr2)
{
    const char *ptr = args;
    int global = 0;
    int print_mode = 0;

    if (!last_sub_pattern || !last_sub_replacement) {
        return 1;
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (!parse_substitute_flags(ptr, &global, &print_mode)) {
        return 1;
    }
    save_undo(b);
    if (addr1 == -1) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (addr1 > 0 && addr2 >= addr1) {
        apply_substitute_range(b, addr1, addr2, last_sub_pattern,
            last_sub_replacement, global, print_mode);
    }
    return 1;
}

int
handle_shell_command(char *cmd)
{
    char *shell;
    pid_t pid;

    if (secure_mode) {
        exvi_report_shell_forbidden();
        return 1;
    }
    shell = getenv("SHELL");
    if (!shell || !*shell) {
        shell = "/bin/sh";
    }
    pid = fork();
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
            if (errno != EINTR) {
                break;
            }
        }
        signal(SIGINT, old_int);
        signal(SIGQUIT, old_quit);
    }
    return 1;
}
