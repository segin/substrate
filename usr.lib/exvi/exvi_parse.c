#include "exvi_internal.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

static char *
skip_ws(char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static char *
skip_delimited_syntax(char *p, char delim)
{
    while (*p) {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == delim) {
            return p + 1;
        }
        p++;
    }
    return p;
}

static int
scan_command_match(const char *cmd, const char *name, const char *abbr,
    const char **argp, int *forcep)
{
    size_t name_len = strlen(name);
    size_t abbr_len = abbr ? strlen(abbr) : 0;
    size_t cmd_len = 0;
    size_t used = 0;

    if (abbr && abbr_len > 0 && !isalpha((unsigned char)abbr[0])
        && strncmp(cmd, abbr, abbr_len) == 0) {
        used = abbr_len;
    } else {
        while (isalpha((unsigned char)cmd[cmd_len])) {
            cmd_len++;
        }
        if (cmd_len > 0) {
            if (cmd_len <= name_len && strncmp(cmd, name, cmd_len) == 0) {
                size_t min_len = (abbr && abbr_len > 0
                    && isalpha((unsigned char)abbr[0])) ? abbr_len : name_len;

                if (cmd_len >= min_len) {
                    used = cmd_len;
                }
            } else if (abbr && abbr_len > 0 && isalpha((unsigned char)abbr[0])
                && cmd_len == abbr_len && strncmp(cmd, abbr, abbr_len) == 0) {
                used = cmd_len;
            }
        }
    }

    if (used == 0) {
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

static char *
skip_address_atom_syntax(char *p)
{
    p = skip_ws(p);

    if (isdigit((unsigned char)*p)) {
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        return p;
    }
    if (*p == '.' || *p == '$') {
        return p + 1;
    }
    if (*p == '\'') {
        if (p[1] >= 'a' && p[1] <= 'z') {
            return p + 2;
        }
        return NULL;
    }
    if (*p == '/' || *p == '?') {
        char delim = *p++;

        return skip_delimited_syntax(p, delim);
    }
    return NULL;
}

static int
address_maybe_starts(char *p)
{
    p = skip_ws(p);
    return isdigit((unsigned char)*p)
        || *p == '.'
        || *p == '$'
        || *p == '\''
        || *p == '/'
        || *p == '?'
        || *p == '+'
        || *p == '-';
}

static char *
skip_address_syntax(char *p, int *matched)
{
    char *start = p;
    char *next;

    p = skip_ws(p);
    if (*p == '+' || *p == '-') {
        do {
            p++;
            p = skip_ws(p);
            while (isdigit((unsigned char)*p)) {
                p++;
            }
            p = skip_ws(p);
        } while (*p == '+' || *p == '-');
        *matched = 1;
        return p;
    }

    next = skip_address_atom_syntax(p);
    if (!next) {
        *matched = 0;
        return start;
    }
    p = next;

    for (;;) {
        char *q = skip_ws(p);

        if (*q != '+' && *q != '-') {
            break;
        }
        p = q + 1;
        p = skip_ws(p);
        while (isdigit((unsigned char)*p)) {
            p++;
        }
    }

    *matched = 1;
    return p;
}

static char *
skip_range_syntax(char *p)
{
    int matched = 0;

    p = skip_ws(p);
    if (*p == '%') {
        return p + 1;
    }

    p = skip_address_syntax(p, &matched);
    if (!matched) {
        return p;
    }

    p = skip_ws(p);
    if (*p == ',' || *p == ';') {
        p++;
        {
            char *next = skip_address_syntax(p, &matched);

            if (matched) {
                p = next;
            }
        }
    }

    return p;
}

static char *
scan_generic_break(char *p, exvi_command_break_t *kind)
{
    while (*p) {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '|') {
            *kind = EXVI_COMMAND_BREAK_SEPARATOR;
            return p;
        }
        if (*p == '"') {
            *kind = EXVI_COMMAND_BREAK_COMMENT;
            return p;
        }
        p++;
    }
    return NULL;
}

static char *
scan_substitute_break(const char *args, exvi_command_break_t *kind)
{
    char *p = skip_ws((char *)args);
    char delim;

    if (*p == '\0') {
        return NULL;
    }

    delim = *p++;
    p = skip_delimited_syntax(p, delim);
    if (*p == '\0') {
        return NULL;
    }
    p = skip_delimited_syntax(p, delim);
    return scan_generic_break(p, kind);
}

static char *
scan_global_break(buffer_t *b, const char *args, exvi_command_break_t *kind)
{
    char *p = skip_ws((char *)args);
    char delim;

    (void)b;
    (void)kind;

    if (*p == '\0') {
        return NULL;
    }

    delim = *p++;
    p = skip_delimited_syntax(p, delim);
    return (*p == '\0') ? NULL : NULL;
}

static int
buf_clamp_line(buffer_t *b, int line_num)
{
    if (line_num < 0) {
        return -1;
    }
    if (line_num == 0) {
        return 0;
    }
    if (line_num > b->line_count) {
        return -1;
    }
    return line_num;
}

char *
parse_delimited_text(char **cmd_ptr, char delim)
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

    free(text);
    return NULL;
}

static int
buf_search_forward(buffer_t *b, const char *pattern)
{
    regex_t re;
    line_t *start;
    line_t *l;

    if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
        return -1;
    }

    start = b->cur ? b->cur->next : b->head;

    l = start;
    while (l) {
        if (regexec(&re, l->text, 0, NULL, 0) == 0) {
            int line_num = 1;
            line_t *scan = b->head;

            while (scan && scan != l) {
                line_num++;
                scan = scan->next;
            }
            regfree(&re);
            return scan ? line_num : -1;
        }
        l = l->next;
    }

    if (option_wrapscan) {
        for (l = b->head; l && l != start; l = l->next) {
            if (regexec(&re, l->text, 0, NULL, 0) == 0) {
                int line_num = 1;
                line_t *scan = b->head;

                while (scan && scan != l) {
                    line_num++;
                    scan = scan->next;
                }
                regfree(&re);
                return scan ? line_num : -1;
            }
        }
    }

    regfree(&re);
    return -1;
}

static int
buf_search_backward(buffer_t *b, const char *pattern)
{
    regex_t re;
    line_t *start;
    line_t *l;

    if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
        return -1;
    }

    start = b->cur ? b->cur->prev : b->tail;

    for (l = start; l; l = l->prev) {
        if (regexec(&re, l->text, 0, NULL, 0) == 0) {
            int line_num = 1;
            line_t *scan = b->head;

            while (scan && scan != l) {
                line_num++;
                scan = scan->next;
            }
            regfree(&re);
            return scan ? line_num : -1;
        }
    }

    if (option_wrapscan) {
        for (l = b->tail; l && l != start; l = l->prev) {
            if (regexec(&re, l->text, 0, NULL, 0) == 0) {
                int line_num = 1;
                line_t *scan = b->head;

                while (scan && scan != l) {
                    line_num++;
                    scan = scan->next;
                }
                regfree(&re);
                return scan ? line_num : -1;
            }
        }
    }

    regfree(&re);
    return -1;
}

int
exvi_search(buffer_t *b, const char *pattern, int forward)
{
    char *search = NULL;
    int addr;

    if (pattern && pattern[0] != '\0') {
        search = strdup(pattern);
    } else if (last_search_pattern) {
        search = strdup(last_search_pattern);
    }
    if (!search) {
        return -1;
    }

    replace_saved_string(&last_search_pattern, search);
    addr = forward ? buf_search_forward(b, search)
                   : buf_search_backward(b, search);
    free(search);
    return addr;
}

static int
parse_address_atom(buffer_t *b, char **cmd_ptr)
{
    char *p = *cmd_ptr;
    int addr = -1;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (isdigit((unsigned char)*p)) {
        addr = strtol(p, &p, 10);
    } else if (*p == '.') {
        addr = buf_current_line(b);
        p++;
    } else if (*p == '$') {
        addr = b->line_count;
        p++;
    } else if (*p == '\'') {
        p++;
        if (*p >= 'a' && *p <= 'z' && b->marks[*p - 'a']) {
            line_t *mark = b->marks[*p - 'a'];
            addr = 1;
            line_t *l = b->head;

            while (l && l != mark) {
                addr++;
                l = l->next;
            }
            if (!l) {
                addr = -1;
            }
            p++;
        } else {
            return -1;
        }
    } else if (*p == '/' || *p == '?') {
        char delim = *p++;
        char *pattern = parse_delimited_text(&p, delim);

        if (!pattern) {
            return -1;
        }
        addr = exvi_search(b, pattern, delim == '/');
        free(pattern);
    }

    if (addr != -1) {
        *cmd_ptr = p;
    }
    return addr;
}

int
parse_address(buffer_t *b, char **cmd_ptr)
{
    char *p = *cmd_ptr;
    int addr;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '+' || *p == '-') {
        addr = buf_current_line(b);
        if (addr == -1) {
            return -1;
        }
    } else {
        addr = parse_address_atom(b, &p);
        if (addr == -1) {
            return -1;
        }
    }

    for (;;) {
        long offset = 1;
        int sign = 0;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '+' || *p == '-') {
            sign = (*p == '+') ? 1 : -1;
            p++;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            if (isdigit((unsigned char)*p)) {
                offset = strtol(p, &p, 10);
            }
            addr += sign * (int)offset;
            if (buf_clamp_line(b, addr) == -1) {
                return -1;
            }
            continue;
        }
        break;
    }

    *cmd_ptr = p;
    return addr;
}

int
parse_address_checked(buffer_t *b, char **cmd_ptr, int *errorp)
{
    int expected = address_maybe_starts(*cmd_ptr);
    int addr = parse_address(b, cmd_ptr);

    if (errorp) {
        *errorp = (expected && addr == -1);
    }
    return addr;
}

int
parse_range_checked(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2,
    int *errorp)
{
    char *p;
    int explicit_range = 0;
    line_t *saved_cur = b->cur;
    int a1;
    int parse_error = 0;

    *addr1 = -1;
    *addr2 = -1;
    p = *cmd_ptr;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '%') {
        *addr1 = (b->line_count > 0) ? 1 : 0;
        *addr2 = b->line_count;
        *cmd_ptr = p + 1;
        b->cur = saved_cur;
        if (errorp) {
            *errorp = 0;
        }
        return 1;
    }

    a1 = parse_address_checked(b, cmd_ptr, &parse_error);
    if (parse_error) {
        b->cur = saved_cur;
        if (errorp) {
            *errorp = 1;
        }
        return 0;
    }
    if (a1 != -1) {
        explicit_range = 1;
        *addr1 = a1;
        *addr2 = a1;

        p = *cmd_ptr;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == ',' || *p == ';') {
            int semicolon = (*p == ';');
            int a2;

            p++;
            *cmd_ptr = p;
            if (semicolon) {
                b->cur = buf_get_line(b, a1);
            }
            a2 = parse_address_checked(b, cmd_ptr, &parse_error);
            if (parse_error) {
                b->cur = saved_cur;
                if (errorp) {
                    *errorp = 1;
                }
                return 0;
            }
            if (a2 != -1) {
                *addr2 = a2;
            } else {
                *addr2 = b->line_count;
            }
        }
    }
    b->cur = saved_cur;
    if (errorp) {
        *errorp = 0;
    }
    return explicit_range;
}

int
parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2)
{
    int error = 0;

    return parse_range_checked(b, cmd_ptr, addr1, addr2, &error);
}

char *
find_command_break(buffer_t *b, char *cmd, exvi_command_break_t *kind)
{
    char *p;
    const char *args = NULL;
    int force = 0;

    *kind = EXVI_COMMAND_BREAK_NONE;
    p = skip_range_syntax(cmd);
    p = skip_ws(p);

    if (*p == '\0') {
        return NULL;
    }
    if (*p == '"') {
        *kind = EXVI_COMMAND_BREAK_COMMENT;
        return p;
    }
    if (*p == '!') {
        return NULL;
    }

    if (scan_command_match(p, "substitute", "s", &args, NULL)) {
        return scan_substitute_break(args, kind);
    }
    if (scan_command_match(p, "global", "g", &args, NULL)
        || scan_command_match(p, "global", "v", &args, NULL)) {
        return scan_global_break(b, args, kind);
    }
    if (scan_command_match(p, "read", "r", &args, NULL)
        || scan_command_match(p, "write", "w", &args, &force)) {
        char *q = skip_ws((char *)args);

        (void)force;
        if (*q == '!') {
            return NULL;
        }
    }

    return scan_generic_break(p, kind);
}
