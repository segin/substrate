#include "exvi_internal.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

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
    if (!start) {
        start = b->head;
    }

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
    if (!start) {
        start = b->tail;
    }

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

    regfree(&re);
    return -1;
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
        if (pattern[0] == '\0') {
            free(pattern);
            pattern = last_search_pattern ? strdup(last_search_pattern) : NULL;
            if (!pattern) {
                return -1;
            }
        }
        replace_saved_string(&last_search_pattern, pattern);
        addr = (delim == '/') ? buf_search_forward(b, pattern)
                              : buf_search_backward(b, pattern);
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
parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2)
{
    char *p;
    int explicit_range = 0;
    line_t *saved_cur = b->cur;
    int a1;

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
        return 1;
    }

    a1 = parse_address(b, cmd_ptr);
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
            a2 = parse_address(b, cmd_ptr);
            if (a2 != -1) {
                *addr2 = a2;
            } else {
                *addr2 = b->line_count;
            }
        }
    }
    b->cur = saved_cur;
    return explicit_range;
}
