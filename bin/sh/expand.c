#include "expand.h"
#include "shell_var.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>

extern long strtol(const char *nptr, char **endptr, int base);
extern int execute_line(char *line);

static char *capture_command_output(const char *cmd_str) {
    int pfd[2];
    if (pipe(pfd) < 0) return strdup("");
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        execute_line((char *)cmd_str);
        _exit(0);
    } else if (pid > 0) {
        close(pfd[1]);
        size_t cap = 1024, len = 0;
        char *buf = malloc(cap);
        char read_buf[256];
        ssize_t n;
        while ((n = read(pfd[0], read_buf, sizeof(read_buf))) > 0) {
            if (len + n >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            memcpy(buf + len, read_buf, n);
            len += n;
        }
        buf[len] = '\0';
        close(pfd[0]);
        waitpid(pid, NULL, 0);
        /* POSIX: strip trailing newlines */
        while (len > 0 && buf[len - 1] == '\n') {
            buf[--len] = '\0';
        }
        return buf;
    } else {
        close(pfd[0]); close(pfd[1]);
        return strdup("");
    }
}

static void buffer_append_internal(char **buf, size_t *cap, size_t *len, char c) {
    if (*len + 1 >= *cap) {
        *cap *= 2;
        if (*cap == 0) *cap = 16;
        *buf = realloc(*buf, *cap);
    }
    (*buf)[*len] = c;
    (*len)++;
    (*buf)[*len] = 0;
}

static void buffer_append_str_internal(char **buf, size_t *cap, size_t *len, const char *str) {
    if (!str) return;
    while (*str) {
        buffer_append_internal(buf, cap, len, *str++);
    }
}

#define QUOTED_BIT 0x80

static void finalize_word(char **cw, size_t *cw_cap, size_t *cw_len, char ***list, size_t *cap, size_t *len, int quoted_any) {
    (void)cw_cap;
    if (*cw_len > 0 || quoted_any) {
        if (*len + 1 >= *cap) {
            *cap *= 2;
            if (*cap == 0) *cap = 16;
            *list = realloc(*list, *cap * sizeof(char *));
        }
        (*list)[(*len)++] = strdup(*cw);
    }
    (*cw)[0] = 0;
    *cw_len = 0;
}

static char *get_ifs() {
    char *ifs = shell_var_get("IFS");
    if (!ifs) return " \t\n";
    return ifs;
}

static void expand_str_split(const char *val, int split, char ***list, size_t *cap, size_t *len, char **cw, size_t *cw_cap, size_t *cw_len) {
    if (!val) return;
    if (!split) {
        // Within double quotes: mask everything to prevent field splitting and globbing
        while (*val) {
            buffer_append_internal(cw, cw_cap, cw_len, (*val++) | QUOTED_BIT);
        }
        return;
    }

    const char *ifs = get_ifs();
    const char *p = val;
    // Skip initial IFS whitespace if splitting
    while (*p && isspace(*p) && strchr(ifs, *p)) p++;

    while (*p) {
        if (strchr(ifs, *p)) {
            finalize_word(cw, cw_cap, cw_len, list, cap, len, 0);
            if (isspace(*p) && strchr(" \t\n", *p)) {
                while (*p && isspace(*p) && strchr(ifs, *p)) p++;
                continue;
            }
        } else {
            buffer_append_internal(cw, cw_cap, cw_len, *p);
        }
        if (*p) p++;
    }
}



static char *lookup_variable(const char *name) {
    return shell_var_get(name);
}

// Full recursive descent parser for POSIX arithmetic
static const char *arith_ptr;

static long parse_assign(void);
static long parse_log_or(void);
static void remove_quotes(char *word);

static long parse_cond(void) {
    long val = parse_log_or();
    while (isspace(*arith_ptr)) arith_ptr++;
    if (*arith_ptr == '?') {
        arith_ptr++;
        long left = parse_assign();
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == ':') {
            arith_ptr++;
            long right = parse_cond();
            return val ? left : right;
        }
    }
    return val;
}

// Logic to be refined... but let's implement common levels
static long parse_unary(void) {
    while (isspace(*arith_ptr)) arith_ptr++;
    if (*arith_ptr == '(') {
        arith_ptr++;
        long val = parse_assign();
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == ')') arith_ptr++;
        return val;
    }
    if (*arith_ptr == '-') { arith_ptr++; return -parse_unary(); }
    if (*arith_ptr == '+') { arith_ptr++; return parse_unary(); }
    if (*arith_ptr == '!') { arith_ptr++; return !parse_unary(); }
    if (*arith_ptr == '~') { arith_ptr++; return ~parse_unary(); }
    
    if (isdigit(*arith_ptr)) {
        char *end;
        long val = strtol(arith_ptr, &end, 0);
        arith_ptr = end;
        return val;
    }
    
    if (isalpha(*arith_ptr) || *arith_ptr == '_') {
        const char *start = arith_ptr;
        while (isalnum(*arith_ptr) || *arith_ptr == '_') arith_ptr++;
        char *name = sh_strndup(start, arith_ptr - start);
        char *val_str = lookup_variable(name);
        long val = 0;
        if (val_str && *val_str) val = strtol(val_str, NULL, 0);
        free(name);
        return val;
    }
    return 0;
}

static long parse_mul(void) {
    long left = parse_unary();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '*' && *(arith_ptr+1) != '=') { arith_ptr++; left *= parse_unary(); }
        else if (*arith_ptr == '/' && *(arith_ptr+1) != '=') { 
            arith_ptr++; long r = parse_unary(); if (r) left /= r; 
        }
        else if (*arith_ptr == '%' && *(arith_ptr+1) != '=') { 
            arith_ptr++; long r = parse_unary(); if (r) left %= r; 
        }
        else break;
    }
    return left;
}

static long parse_add(void) {
    long left = parse_mul();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '+' && *(arith_ptr+1) != '+' && *(arith_ptr+1) != '=') { arith_ptr++; left += parse_mul(); }
        else if (*arith_ptr == '-' && *(arith_ptr+1) != '-' && *(arith_ptr+1) != '=') { arith_ptr++; left -= parse_mul(); }
        else break;
    }
    return left;
}

static long parse_shift(void) {
    long left = parse_add();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (strncmp(arith_ptr, "<<", 2) == 0 && *(arith_ptr+2) != '=') { arith_ptr += 2; left <<= parse_add(); }
        else if (strncmp(arith_ptr, ">>", 2) == 0 && *(arith_ptr+2) != '=') { arith_ptr += 2; left >>= parse_add(); }
        else break;
    }
    return left;
}

static long parse_rel(void) {
    long left = parse_shift();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (strncmp(arith_ptr, "<=", 2) == 0) { arith_ptr += 2; left = (left <= parse_shift()); }
        else if (strncmp(arith_ptr, ">=", 2) == 0) { arith_ptr += 2; left = (left >= parse_shift()); }
        else if (*arith_ptr == '<' && *(arith_ptr+1) != '<') { arith_ptr++; left = (left < parse_shift()); }
        else if (*arith_ptr == '>' && *(arith_ptr+1) != '>') { arith_ptr++; left = (left > parse_shift()); }
        else break;
    }
    return left;
}

static long parse_eq(void) {
    long left = parse_rel();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (strncmp(arith_ptr, "==", 2) == 0) { arith_ptr += 2; left = (left == parse_rel()); }
        else if (strncmp(arith_ptr, "!=", 2) == 0) { arith_ptr += 2; left = (left != parse_rel()); }
        else break;
    }
    return left;
}

static long parse_bit_and(void) {
    long left = parse_eq();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '&' && *(arith_ptr+1) != '&' && *(arith_ptr+1) != '=') { arith_ptr++; left &= parse_eq(); }
        else break;
    }
    return left;
}

static long parse_bit_xor(void) {
    long left = parse_bit_and();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '^' && *(arith_ptr+1) != '=') { arith_ptr++; left ^= parse_bit_and(); }
        else break;
    }
    return left;
}

static long parse_bit_or(void) {
    long left = parse_bit_xor();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '|' && *(arith_ptr+1) != '|' && *(arith_ptr+1) != '=') { arith_ptr++; left |= parse_bit_xor(); }
        else break;
    }
    return left;
}

static long parse_log_and(void) {
    long left = parse_bit_or();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (strncmp(arith_ptr, "&&", 2) == 0) { arith_ptr += 2; left = left && parse_bit_or(); }
        else break;
    }
    return left;
}

static long parse_log_or(void) {
    long left = parse_log_and();
    while (1) {
        while (isspace(*arith_ptr)) arith_ptr++;
        if (strncmp(arith_ptr, "||", 2) == 0) { arith_ptr += 2; left = left || parse_log_and(); }
        else break;
    }
    return left;
}

static long parse_assign(void) {
    while (isspace(*arith_ptr)) arith_ptr++;
    const char *saved_ptr = arith_ptr;
    if (isalpha(*arith_ptr) || *arith_ptr == '_') {
        const char *start = arith_ptr;
        while (isalnum(*arith_ptr) || *arith_ptr == '_') arith_ptr++;
        const char *end = arith_ptr;
        while (isspace(*arith_ptr)) arith_ptr++;
        if (*arith_ptr == '=' && *(arith_ptr+1) != '=') {
            char *name = sh_strndup(start, end - start);
            arith_ptr++;
            long val = parse_assign();
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", val);
            shell_var_set(name, buf);
            free(name);
            return val;
        }
        arith_ptr = saved_ptr;
    }
    return parse_cond();
}





// Check if string contains glob characters
static int is_glob(const char *s) {
    if (!s) return 0;
    while (*s) {
        if (!((*s) & QUOTED_BIT)) {
            char c = *s;
            if (c == '*' || c == '?' || c == '[') return 1;
        }
        s++;
    }
    return 0;
}

static int glob_match(const char *pattern, const char *name) {
    if (!*pattern) return !*name;
    unsigned char p = (unsigned char)*pattern;
    if (!(p & QUOTED_BIT)) {
        if (p == '*') {
            if (glob_match(pattern + 1, name)) return 1;
            if (*name && glob_match(pattern, name + 1)) return 1;
            return 0;
        }
        if (p == '?') {
            if (!*name) return 0;
            return glob_match(pattern + 1, name + 1);
        }
        // [ matching could be added here
    }
    if (*name && (*name == (char)(p & ~QUOTED_BIT))) {
        return glob_match(pattern + 1, name + 1);
    }
    return 0;
}

// Compare function for qsort
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// Simple glob expansion: only handles current directory or single directory component for now
// To fully implement globbing like /bin/* or src/*.c, we need to split by / and recurse.
// For now, let's assume patterns are logically applied to current directory or handle basic / splitting?
// Implementing full glob path traversal is complex.
// Let's implement a version that handles:
// 1. If pattern contains /, split into dir and pattern?
// 2. OR just handle current directory matching for simple patterns (e.g. *.c)
// Given the requirements and "Globbing (*, ?, [...])", typically implies simple matching first.
// Let's implement full recursive globbing later if needed, but for now:
// Support simple patterns in current dir, AND patterns with simple directory prefix e.g. "dir/*.c"
// Actually, standard sh expansion handles splitting.
// Let's stick to: if pattern has /, use a simplified approach or just directory iteration.
// Simplified approach:
// Scan current directory. Match against pattern.
// If pattern starts with /, we need to scan root?
// Let's implement:
// if pattern has no /, scan .; match pattern.
// if pattern has /, separate into directory part and file part?
// e.g. "foo/*.c" -> dir="foo", pattern="*.c".
// This covers most cases.

static void expand_glob_recursive(char ***list, size_t *cap, size_t *len, const char *prefix, const char *pattern) {
    if (!pattern || !*pattern) {
        if (*len + 1 >= *cap) {
            *cap *= 2;
            if (*cap == 0) *cap = 16;
            *list = realloc(*list, *cap * sizeof(char *));
        }
        (*list)[(*len)++] = strdup(prefix);
        return;
    }

    const char *slash = pattern;
    while (*slash && ((*slash & ~QUOTED_BIT) != '/')) slash++;
    
    char *component;
    const char *remainder;
    if (*slash) {
        component = sh_strndup(pattern, slash - pattern);
        remainder = slash + 1;
    } else {
        component = strdup(pattern);
        remainder = NULL;
    }

    if (!is_glob(component)) {
        char *unquoted_comp = strdup(component);
        remove_quotes(unquoted_comp);
        char *new_prefix;
        if (!prefix || !*prefix) {
            new_prefix = strdup(unquoted_comp);
        } else if (strcmp(prefix, "/") == 0) {
            new_prefix = malloc(strlen(unquoted_comp) + 2);
            sprintf(new_prefix, "/%s", unquoted_comp);
        } else {
            new_prefix = malloc(strlen(prefix) + strlen(unquoted_comp) + 2);
            sprintf(new_prefix, "%s/%s", prefix, unquoted_comp);
        }
        expand_glob_recursive(list, cap, len, new_prefix, remainder);
        free(new_prefix);
        free(unquoted_comp);
        free(component);
        return;
    }

    const char *search_dir = (prefix && *prefix) ? prefix : ".";
    DIR *d = opendir(search_dir);
    if (!d) {
        free(component);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' && (component[0] & ~QUOTED_BIT) != '.') continue;
        if (glob_match(component, ent->d_name)) {
            char *new_prefix;
            if (!prefix || !*prefix) {
                new_prefix = strdup(ent->d_name);
            } else if (strcmp(prefix, "/") == 0) {
                new_prefix = malloc(strlen(ent->d_name) + 2);
                sprintf(new_prefix, "/%s", ent->d_name);
            } else {
                new_prefix = malloc(strlen(prefix) + strlen(ent->d_name) + 2);
                sprintf(new_prefix, "%s/%s", prefix, ent->d_name);
            }
            expand_glob_recursive(list, cap, len, new_prefix, remainder);
            free(new_prefix);
        }
    }
    closedir(d);
    free(component);
}

static void glob_word(const char *pattern, char ***list, size_t *cap, size_t *len) {
    if (!is_glob(pattern)) {
        if (*len + 1 >= *cap) {
            *cap *= 2;
            if (*cap == 0) *cap = 16;
            *list = realloc(*list, *cap * sizeof(char *));
        }
        (*list)[(*len)++] = strdup(pattern);
        return;
    }

    size_t start_len = *len;
    const char *prefix = "";
    const char *remain = pattern;
    
    if ((*pattern & ~QUOTED_BIT) == '/') {
        prefix = "/";
        remain = pattern + 1;
    }
    
    expand_glob_recursive(list, cap, len, prefix, remain);
    
    if (*len == start_len) {
        if (*len + 1 >= *cap) { *cap *= 2; *list = realloc(*list, *cap * sizeof(char *)); }
        (*list)[(*len)++] = strdup(pattern);
    } else {
        qsort((*list) + start_len, *len - start_len, sizeof(char *), compare_strings);
    }
}

static void expand_word_internal(const char *word, char ***list, size_t *cap, size_t *len, int split) {
    (void)split;
    if (!word) return;
    size_t cw_cap = 32, cw_len = 0;
    char *cw = malloc(cw_cap); cw[0] = 0;
    int in_sq = 0, in_dq = 0, escape = 0, quoted_any = 0;
    const char *p = word;
    if (*p == '~' && !in_sq && !in_dq) {
        char *home = lookup_variable("HOME");
        if (home) {
            expand_str_split(home, 0, list, cap, len, &cw, &cw_cap, &cw_len);
            p++;
        }
    }
    while (*p) {
        char c = *p;
        if (escape) { buffer_append_internal(&cw, &cw_cap, &cw_len, c | QUOTED_BIT); escape = 0; }
        else if (in_sq) {
            if (c == '\'') { in_sq = 0; quoted_any = 1; }
            else { buffer_append_internal(&cw, &cw_cap, &cw_len, c | QUOTED_BIT); quoted_any = 1; }
        }
        else if (c == '\\') {
            if (in_dq) {
                char next = *(p + 1);
                if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') { escape = 1; p++; continue; }
                buffer_append_internal(&cw, &cw_cap, &cw_len, c | QUOTED_BIT);
            } else { escape = 1; p++; continue; }
        } else if (c == '\'') {
            if (!in_dq) { in_sq = 1; quoted_any = 1; }
            else { buffer_append_internal(&cw, &cw_cap, &cw_len, c | QUOTED_BIT); }
        }
        else if (c == '"') {
            if (!in_sq) { in_dq = !in_dq; quoted_any = 1; }
            else { buffer_append_internal(&cw, &cw_cap, &cw_len, c | QUOTED_BIT); }
        }
        else if (c == '$' && !in_sq) {
            p++;
            if (*p == '(' && *(p+1) == '(') {
                p += 2;
                const char *start = p;
                int depth = 1;
                while (*p && depth > 0) {
                    if (*p == '(') depth++;
                    else if (*p == ')') {
                        if (depth == 1 && *(p+1) == ')') { depth--; p++; }
                        else depth--;
                    }
                    if (depth > 0) p++;
                }
                if (depth == 0) {
                    char *expr = sh_strndup(start, p - start - 1);
                    arith_ptr = expr;
                    long val = parse_assign();
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%ld", val);
                    expand_str_split(buf, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                    free(expr);
                }
            } else if (*p == '(') {
                p++;
                const char *start = p;
                int depth = 1;
                while (*p && depth > 0) {
                    if (*p == '(') depth++;
                    else if (*p == ')') depth--;
                    if (depth > 0) p++;
                }
                if (depth == 0) {
                    char *cmd = sh_strndup(start, p - start);
                    char *output = capture_command_output(cmd);
                    expand_str_split(output, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                    free(output);
                    free(cmd);
                }
            } else if (*p == '{') {
                p++;
                const char *start = p;
                // Find matching closing brace, accounting for nested ${...}
                int brace_depth = 1;
                while (*p && brace_depth > 0) {
                    if (*p == '$' && *(p+1) == '{') {
                        brace_depth++;
                        p += 2;
                    } else if (*p == '}') {
                        brace_depth--;
                        if (brace_depth > 0) p++;
                    } else {
                        p++;
                    }
                }
                if (brace_depth == 0) {
                    // Extract content between ${...}
                    char *content = sh_strndup(start, p - start);
                    
                    // Parse: varname possibly followed by operator and word
                    // POSIX operators: :-, :=, :?, :+, -, =, ?, +, #, ##, %, %%
                    const char *c = content;
                    char *varname = NULL;
                    
                    // Special parameters can be single chars: #, !, ?, $, *, @, 0-9, -
                    if (*c == '#' && (c[1] == '\0' || c[1] == ':' || c[1] == '-' || c[1] == '=' || c[1] == '?' || c[1] == '+' || c[1] == '%' || c[1] == '#')) {
                        varname = strdup("#");
                        c++;
                    } else if (*c == '!' || *c == '?' || *c == '$' || *c == '-' || *c == '@' || *c == '*') {
                        varname = sh_strndup(c, 1);
                        c++;
                    } else if (isdigit(*c)) {
                        varname = sh_strndup(c, 1);
                        c++;
                    } else {
                        // Regular varname
                        const char *name_start = c;
                        while (*c && (isalnum(*c) || *c == '_')) c++;
                        if (c > name_start) {
                            varname = sh_strndup(name_start, c - name_start);
                        }
                    }
                    
                    char *val = varname ? lookup_variable(varname) : NULL;
                    int is_set = (val != NULL);
                    int is_nonnull = (val && val[0]);
                    
                    // Check for operators
                    if (*c == ':' && (c[1] == '-' || c[1] == '=' || c[1] == '?' || c[1] == '+')) {
                        // :- :+ := :? operators (check null flag)
                        char op = c[1];
                        const char *word = c + 2;
                        
                        if (op == '-') {
                            // ${var:-word}: use word if unset or null
                            if (!is_nonnull) {
                                // Recursively expand the word
                                char *expanded = expand_word(word);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        } else if (op == '+') {
                            // ${var:+word}: use word if set and non-null
                            if (is_nonnull) {
                                char *expanded = expand_word(word);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            }
                        } else if (op == '=') {
                            // ${var:=word}: assign and use word if unset or null
                            if (!is_nonnull && varname) {
                                char *expanded = expand_word(word);
                                shell_var_set(varname, expanded);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        } else if (op == '?') {
                            // ${var:?word}: error if unset or null
                            if (!is_nonnull) {
                                char *expanded = expand_word(word);
                                fprintf(stderr, "%s: %s: %s\n", shell_var_get_name(), varname ? varname : "", expanded);
                                free(expanded);
                                // Could exit here for non-interactive
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        }
                    } else if (*c == '-' || *c == '=' || *c == '?' || *c == '+') {
                        // - + = ? operators (only check unset)
                        char op = *c;
                        const char *word = c + 1;
                        
                        if (op == '-') {
                            if (!is_set) {
                                char *expanded = expand_word(word);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        } else if (op == '+') {
                            if (is_set) {
                                char *expanded = expand_word(word);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            }
                        } else if (op == '=') {
                            if (!is_set && varname) {
                                char *expanded = expand_word(word);
                                shell_var_set(varname, expanded);
                                expand_str_split(expanded, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                                free(expanded);
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        } else if (op == '?') {
                            if (!is_set) {
                                char *expanded = expand_word(word);
                                fprintf(stderr, "%s: %s: %s\n", shell_var_get_name(), varname ? varname : "", expanded);
                                free(expanded);
                            } else {
                                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        }
                    } else if (*c == '#') {
                        // ${#var} or ${var#pattern} / ${var##pattern}
                        if (c == content && c[1] && (isalpha(c[1]) || c[1] == '_')) {
                            // ${#var} - length
                            c++;
                            char *length_var = strdup(c);
                            char *length_val = lookup_variable(length_var);
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%zu", length_val ? strlen(length_val) : 0);
                            expand_str_split(buf, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            free(length_var);
                        } else {
                            // ${var#pattern} or ${var##pattern}
                            int greedy = (c[1] == '#');
                            const char *pattern = greedy ? c + 2 : c + 1;
                            if (val) {
                                // Remove shortest/longest prefix matching pattern
                                size_t vlen = strlen(val);
                                size_t match_len = 0;
                                for (size_t i = 0; i <= vlen; i++) {
                                    char tmp = val[i];
                                    val[i] = '\0';
                                    if (match_pattern(pattern, val)) {
                                        match_len = i;
                                        if (!greedy) { val[i] = tmp; break; }
                                    }
                                    val[i] = tmp;
                                }
                                expand_str_split(val + match_len, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            }
                        }
                    } else if (*c == '%') {
                        // ${var%pattern} or ${var%%pattern}
                        int greedy = (c[1] == '%');
                        const char *pattern = greedy ? c + 2 : c + 1;
                        if (val) {
                            // Remove shortest/longest suffix matching pattern
                            size_t vlen = strlen(val);
                            size_t best = vlen;
                            for (size_t i = 0; i <= vlen; i++) {
                                if (match_pattern(pattern, val + vlen - i)) {
                                    best = vlen - i;
                                    if (!greedy) break;
                                }
                            }
                            char *trimmed = sh_strndup(val, best);
                            expand_str_split(trimmed, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                            free(trimmed);
                        }
                    } else {
                        // No operator, just expand the variable
                        expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                    }
                    
                    free(varname);
                    free(content);
                }
            } else if (isalpha(*p) || *p == '_' || isdigit(*p) || *p == '?' || *p == '$' || *p == '#' || *p == '@' || *p == '*' || *p == '-' || *p == '!') {
                const char *start = p;
                if (isdigit(*p) || *p == '?' || *p == '$' || *p == '#' || *p == '@' || *p == '*' || *p == '-' || *p == '!') p++;
                else while (*p && (isalnum(*p) || *p == '_')) p++;
                char *name = sh_strndup(start, p - start);
                char *val = lookup_variable(name);
                expand_str_split(val, !in_dq, list, cap, len, &cw, &cw_cap, &cw_len);
                free(name); p--;
            } else {
                buffer_append_internal(&cw, &cw_cap, &cw_len, '$');
                p--;
            }
        } else {
            buffer_append_internal(&cw, &cw_cap, &cw_len, in_dq ? (c | QUOTED_BIT) : c);
        }
        p++;
    }
    finalize_word(&cw, &cw_cap, &cw_len, list, cap, len, quoted_any);
    free(cw);
}
static void remove_quotes(char *word) {
    if (!word) return;
    char *src = word;
    char *dest = word;
    while (*src) {
        *dest++ = (*src++) & ~QUOTED_BIT;
    }
    *dest = 0;
}

char *expand_word(const char *word) {
    size_t cap = 4, len = 0;
    char **list = malloc(cap * sizeof(char *));
    expand_word_internal(word, &list, &cap, &len, 0); // No splitting
    if (len == 0) { free(list); return strdup(""); }
    char *res = strdup(list[0]);
    remove_quotes(res);
    for (size_t i = 0; i < len; i++) free(list[i]);
    free(list);
    return res;
}

char **expand_list(char **words) {
    if (!words) return NULL;
    size_t cap = 16, len = 0;
    char **expanded = malloc(cap * sizeof(char *));
    for (size_t i = 0; words[i]; i++) {
        expand_word_internal(words[i], &expanded, &cap, &len, 1);
    }
    size_t g_cap = len + 1, g_len = 0;
    char **globbed = malloc(g_cap * sizeof(char *));
    for (size_t i = 0; i < len; i++) {
        glob_word(expanded[i], &globbed, &g_cap, &g_len);
        free(expanded[i]);
    }
    free(expanded);
    for (size_t i = 0; i < g_len; i++) {
        remove_quotes(globbed[i]);
    }
    globbed[g_len] = NULL;
    return globbed;
}

char *expand_heredoc(const char *content, int quoted) {
    if (!content) return strdup("");
    if (quoted) return strdup(content);
    
    size_t cap = strlen(content) + 1;
    size_t len = 0;
    char *buf = malloc(cap);
    buf[0] = 0;
    
    const char *p = content;
    while (*p) {
        if (*p == '\\') {
            char next = *(p + 1);
            if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') {
                if (next != '\n') buffer_append_internal(&buf, &cap, &len, next);
                p += 2;
                continue;
            }
            buffer_append_internal(&buf, &cap, &len, '\\');
        } else if (*p == '$') {
            p++;
            if (*p == '(' && *(p+1) == '(') {
                 // Arithmetic
                 p += 2;
                 const char *start = p;
                 int depth = 1;
                 while (*p && depth > 0) {
                     if (*p == '(') depth++;
                     else if (*p == ')') {
                         if (depth == 1 && *(p+1) == ')') { depth--; p++; }
                         else depth--;
                     }
                     if (depth > 0) p++;
                 }
                 if (depth == 0) {
                     char *expr = sh_strndup(start, p - start - 1);
                     arith_ptr = expr;
                     long val = parse_assign();
                     char val_buf[32];
                     snprintf(val_buf, sizeof(val_buf), "%ld", val);
                     buffer_append_str_internal(&buf, &cap, &len, val_buf);
                     free(expr);
                 }
            } else if (*p == '(') {
                 // Command sub
                 p++;
                 const char *start = p;
                 int depth = 1;
                 while (*p && depth > 0) {
                     if (*p == '(') depth++;
                     else if (*p == ')') depth--;
                     if (depth > 0) p++;
                 }
                 if (depth == 0) {
                     char *cmd = sh_strndup(start, p - start);
                     char *output = capture_command_output(cmd);
                     buffer_append_str_internal(&buf, &cap, &len, output);
                     free(output);
                     free(cmd);
                 }
            } else if (*p == '{') {
                p++; const char *start = p;
                while (*p && *p != '}') p++;
                if (*p == '}') {
                    char *name = sh_strndup(start, p - start);
                    char *val = lookup_variable(name);
                    if (val) buffer_append_str_internal(&buf, &cap, &len, val);
                    free(name);
                }
            } else if (isalpha(*p) || *p == '_' || isdigit(*p) || *p == '?' || *p == '$' || *p == '#' || *p == '@' || *p == '*' || *p == '-' || *p == '!') {
                const char *start = p;
                if (isdigit(*p) || *p == '?' || *p == '$' || *p == '#' || *p == '@' || *p == '*' || *p == '-' || *p == '!') p++;
                else while (*p && (isalnum(*p) || *p == '_')) p++;
                char *name = sh_strndup(start, p - start);
                char *val = lookup_variable(name);
                if (val) buffer_append_str_internal(&buf, &cap, &len, val);
                free(name); p--;
            } else {
                buffer_append_internal(&buf, &cap, &len, '$');
                p--;
            }
        } else {
            buffer_append_internal(&buf, &cap, &len, *p);
        }
        p++;
    }
    return buf;
}
