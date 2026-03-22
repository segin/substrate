#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <time.h>
#include "prompt.h"
#include "util.h"
#include "expand.h"
#include "shell_var.h"
#include "exec.h"
#include "job.h"

char *expand_prompt_escapes(const char *ps1, int command_count, int extended, int depth) {
    if (!ps1) return NULL;
    if (depth > 10) return strdup(ps1); // Recursion limit reached, return literal
    
    // Guard against exponential expansion attacks - max 64KB output
    #define PROMPT_MAX_OUTPUT_SIZE (64 * 1024)
    
    size_t cap = strlen(ps1) * 2 + 16;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    
    const char *p = ps1;
    while (*p) {
        if (*p == '\\') {
            p++;
            if (!*p) {
                buffer_append(&buf, &cap, &len, '\\');
                break;
            }
            switch (*p) {
                case '!': {
                    char num[32];
                    snprintf(num, sizeof(num), "%d", command_count);
                    buffer_append_str(&buf, &cap, &len, num);
                    break;
                }
                case '$': {
                    char c = (geteuid() == 0) ? '#' : '$';
                    buffer_append(&buf, &cap, &len, c);
                    break;
                }
                case '\\':
                    buffer_append_str(&buf, &cap, &len, "\\\\");
                    break;
                case 'u': {
                    struct passwd *pw = getpwuid(getuid());
                    if (pw && pw->pw_name) {
                        buffer_append_str(&buf, &cap, &len, pw->pw_name);
                    } else {
                        char num[32];
                        snprintf(num, sizeof(num), "%d", getuid());
                        buffer_append_str(&buf, &cap, &len, num);
                    }
                    break;
                }
                case 'h': {
                    char hostname[256];
                    if (gethostname(hostname, sizeof(hostname)) == 0) {
                        hostname[sizeof(hostname) - 1] = '\0';
                        for (int i = 0; hostname[i]; i++) {
                            if (hostname[i] == '.') {
                                hostname[i] = '\0';
                                break;
                            }
                        }
                        buffer_append_str(&buf, &cap, &len, hostname);
                    } else {
                        buffer_append_str(&buf, &cap, &len, "unknown");
                    }
                    break;
                }
                case 'w': {
                    char cwd[1024];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char *home = getenv("HOME");
                        if (home && strncmp(cwd, home, strlen(home)) == 0) {
                            buffer_append(&buf, &cap, &len, '~');
                            buffer_append_str(&buf, &cap, &len, cwd + strlen(home));
                        } else {
                            buffer_append_str(&buf, &cap, &len, cwd);
                        }
                    } else {
                        buffer_append_str(&buf, &cap, &len, "?");
                    }
                    break;
                }
                default:
                    buffer_append(&buf, &cap, &len, *p);
                    break;
            }
        } else if (extended && *p == '%') {
            p++;
            if (!*p) {
                buffer_append(&buf, &cap, &len, '%');
                break;
            }
            switch (*p) {
                case '%':
                    buffer_append(&buf, &cap, &len, '%');
                    break;
                case 'n': { // %n = \u
                    struct passwd *pw = getpwuid(getuid());
                    if (pw && pw->pw_name) {
                        buffer_append_str(&buf, &cap, &len, pw->pw_name);
                    } else {
                        char num[32];
                        snprintf(num, sizeof(num), "%d", getuid());
                        buffer_append_str(&buf, &cap, &len, num);
                    }
                    break;
                }
                case 'm': { // %m = \h (short hostname)
                    char hostname[256];
                    if (gethostname(hostname, sizeof(hostname)) == 0) {
                        hostname[sizeof(hostname) - 1] = '\0';
                        for (int i = 0; hostname[i]; i++) {
                            if (hostname[i] == '.') {
                                hostname[i] = '\0';
                                break;
                            }
                        }
                        buffer_append_str(&buf, &cap, &len, hostname);
                    } else {
                        buffer_append_str(&buf, &cap, &len, "unknown");
                    }
                    break;
                }
                case '~': { // %~ = \w
                    char cwd[1024];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char *home = getenv("HOME");
                        if (home && strncmp(cwd, home, strlen(home)) == 0) {
                            buffer_append(&buf, &cap, &len, '~');
                            buffer_append_str(&buf, &cap, &len, cwd + strlen(home));
                        } else {
                            buffer_append_str(&buf, &cap, &len, cwd);
                        }
                    } else {
                        buffer_append_str(&buf, &cap, &len, "?");
                    }
                    break;
                }
                case '#': { // %# = \$
                    char c = (geteuid() == 0) ? '#' : '%';
                    buffer_append(&buf, &cap, &len, c);
                    break;
                }
                case '?': { // %? = exit status
                    char *stat = shell_var_get("?");
                    if (stat) buffer_append_str(&buf, &cap, &len, stat);
                    else buffer_append_str(&buf, &cap, &len, "0");
                    break;
                }

                case 'F': { // %F{color}
                    const char *q = p + 1;
                    int valid = 0;
                    if (*q == '{') {
                        q++;
                        char color[32];
                        int i = 0;
                        while (*q && *q != '}' && i < 31) {
                            color[i++] = *q++;
                        }
                        color[i] = 0;
                        if (*q == '}') {
                            // Map color to ANSI
                            const char *code = NULL;
                            if (strcmp(color, "red") == 0) code = "\001\033[31m\002";
                            else if (strcmp(color, "green") == 0) code = "\001\033[32m\002";
                            else if (strcmp(color, "yellow") == 0) code = "\001\033[33m\002";
                            else if (strcmp(color, "blue") == 0) code = "\001\033[34m\002";
                            else if (strcmp(color, "magenta") == 0) code = "\001\033[35m\002";
                            else if (strcmp(color, "cyan") == 0) code = "\001\033[36m\002";
                            else if (strcmp(color, "white") == 0) code = "\001\033[37m\002";
                            else if (strcmp(color, "black") == 0) code = "\001\033[30m\002";
                            else if (strcmp(color, "default") == 0) code = "\001\033[39m\002";
                            
                            if (code) {
                                buffer_append_str(&buf, &cap, &len, code);
                            }
                            // Even if code is invalid (unknown color), we consumed the tokens.
                            // Although zsh prints nothing for invalid colors, it consumes the sequence.
                            p = q; // Advance p to '}'
                            valid = 1;
                        }
                    }
                    
                    if (!valid) {
                         // Malformed or no brace, treat as literal %F
                         buffer_append(&buf, &cap, &len, '%');
                         buffer_append(&buf, &cap, &len, 'F');
                         // p is left at 'F', loop continues to next char/brace
                    }
                    break;
                }
                case 'f':
                    buffer_append_str(&buf, &cap, &len, "\001\033[39m\002");
                    break;
                case 'K': { // %K{color}
                    const char *q = p + 1;
                    int valid = 0;
                    if (*q == '{') {
                        q++;
                        char color[32];
                        int i = 0;
                        while (*q && *q != '}' && i < 31) {
                            color[i++] = *q++;
                        }
                        color[i] = 0;
                        if (*q == '}') {
                            // Map color to ANSI
                            const char *code = NULL;
                            if (strcmp(color, "red") == 0) code = "\001\033[41m\002";
                            else if (strcmp(color, "green") == 0) code = "\001\033[42m\002";
                            else if (strcmp(color, "yellow") == 0) code = "\001\033[43m\002";
                            else if (strcmp(color, "blue") == 0) code = "\001\033[44m\002";
                            else if (strcmp(color, "magenta") == 0) code = "\001\033[45m\002";
                            else if (strcmp(color, "cyan") == 0) code = "\001\033[46m\002";
                            else if (strcmp(color, "white") == 0) code = "\001\033[47m\002";
                            else if (strcmp(color, "black") == 0) code = "\001\033[40m\002";
                            else if (strcmp(color, "default") == 0) code = "\001\033[49m\002";
                            
                            if (code) {
                                buffer_append_str(&buf, &cap, &len, code);
                            }
                            p = q;
                            valid = 1;
                        }
                    }

                    if (!valid) {
                         buffer_append(&buf, &cap, &len, '%');
                         buffer_append(&buf, &cap, &len, 'K');
                    }
                    break;
                }
                case 'k':
                    buffer_append_str(&buf, &cap, &len, "\001\033[49m\002");
                    break;
                case '(': { /* %(cond.true.false) format: %(C<sep>TRUE<sep>FALSE) where C is condition char (? or #) and <sep> is the character immediately following C */
                    char cond_char = *(p + 1);
                    if (!cond_char) {
                        buffer_append(&buf, &cap, &len, '%');
                        buffer_append(&buf, &cap, &len, '(');
                        break;
                    }

                    char sep = *(p + 2);
                    if (!sep) {
                        buffer_append(&buf, &cap, &len, '%');
                        buffer_append(&buf, &cap, &len, '(');
                        // Do not p++ here, let loop increment p to point to cond_char
                        break;
                    }

                    // Parse TRUE block
                    char true_block[1024] = {0};
                    int ti = 0;
                    const char *q = p + 3;
                    while (*q && *q != sep && ti < 1023) {
                       true_block[ti++] = *q++; 
                    }
                    true_block[ti] = 0;

                    if (*q != sep) {
                        // Malformed
                        buffer_append(&buf, &cap, &len, '%');
                        buffer_append(&buf, &cap, &len, '(');
                        // Do not p++
                        break;
                    }
                    q++; // consume separator

                    // Parse FALSE block
                    char false_block[1024] = {0};
                    int fi = 0;
                    while (*q && *q != ')' && fi < 1023) {
                        false_block[fi++] = *q++;
                    }
                    false_block[fi] = 0;

                    if (*q != ')') {
                        // Malformed
                        buffer_append(&buf, &cap, &len, '%');
                        buffer_append(&buf, &cap, &len, '(');
                        // Do not p++
                         break;
                    }

                    // Evaluate condition
                    int cond_true = 0;
                    if (cond_char == '?') {
                         char *stat = shell_var_get("?");
                         if (stat && strcmp(stat, "0") != 0) cond_true = 1;
                    } else if (cond_char == '#') {
                        if (geteuid() == 0) cond_true = 1;
                    } // else unknown condition treated as false

                    char *sub = expand_prompt_escapes(cond_true ? true_block : false_block, command_count, extended, depth + 1);
                    if (sub) {
                        buffer_append_str(&buf, &cap, &len, sub);
                        free(sub);
                    }
                    
                    p = q; // advance p to ')'
                    break;
                }
                case 'M': { // %M = full hostname
                    char hostname[256];
                    if (gethostname(hostname, sizeof(hostname)) == 0) {
                        buffer_append_str(&buf, &cap, &len, hostname);
                    } else {
                        buffer_append_str(&buf, &cap, &len, "unknown");
                    }
                    break;
                }
                case 'L': { // %L = shell nesting level (SHLVL)
                    char *shlvl = getenv("SHLVL");
                    if (shlvl) {
                        buffer_append_str(&buf, &cap, &len, shlvl);
                    } else {
                        buffer_append_str(&buf, &cap, &len, "1");
                    }
                    break;
                }
                case 'j': { // %j = number of background jobs
                    extern job_t *first_job;
                    int job_count = 0;
                    job_t *j = first_job;
                    while (j) {
                        // Count running/stopped jobs (not completed)
                        if (!job_is_completed(j)) {
                            job_count++;
                        }
                        j = j->next;
                    }
                    char num[32];
                    snprintf(num, sizeof(num), "%d", job_count);
                    buffer_append_str(&buf, &cap, &len, num);
                    break;
                }
                case 'B': // %B = bold on
                    buffer_append_str(&buf, &cap, &len, "\001\033[1m\002");
                    break;
                case 'b': // %b = bold off
                    buffer_append_str(&buf, &cap, &len, "\001\033[22m\002");
                    break;
                case 'U': // %U = underline on
                    buffer_append_str(&buf, &cap, &len, "\001\033[4m\002");
                    break;
                case 'u': // %u = underline off
                    buffer_append_str(&buf, &cap, &len, "\001\033[24m\002");
                    break;
                case 'T': { // %T = 24-hour time HH:MM (no padding)
                    time_t now = time(NULL);
                    struct tm tm_buf;
                    struct tm *tm_info = localtime_r(&now, &tm_buf);
                    if (tm_info) {
                        char tstr[32];
                        snprintf(tstr, sizeof(tstr), "%d:%02d", 
                                tm_info->tm_hour, tm_info->tm_min);
                        buffer_append_str(&buf, &cap, &len, tstr);
                    }
                    break;
                }
                case 'D': { // %D = YY-MM-DD
                    time_t now = time(NULL);
                    struct tm tm_buf;
                    struct tm *tm_info = localtime_r(&now, &tm_buf);
                    if (tm_info) {
                        char dstr[32];
                        snprintf(dstr, sizeof(dstr), "%02d-%02d-%02d", 
                                tm_info->tm_year % 100, tm_info->tm_mon + 1, tm_info->tm_mday);
                        buffer_append_str(&buf, &cap, &len, dstr);
                    }
                    break;
                }
                default:
                    // Treat unknown %x as literal %x
                    buffer_append(&buf, &cap, &len, '%');
                    buffer_append(&buf, &cap, &len, *p);
                    break;
            }
        } else {
            buffer_append(&buf, &cap, &len, *p);
        }
        p++;
    }
    buf[len] = 0;
    return buf;
}

char *evaluate_prompt(const char *ps1, int command_count, int extended) {
    if (!ps1) return NULL;
    
    // Block signals during prompt evaluation for safety
    sigset_t blocked, old_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blocked, &old_mask);
    
    // Save state
    int saved_xtrace = shell_xtrace;
    int saved_errexit = shell_errexit;
    char *saved_status_str = shell_var_get("?");
    char *saved_status = saved_status_str ? strdup(saved_status_str) : NULL;
    
    // Disable tracing and error exit for prompt evaluation
    shell_xtrace = 0;
    shell_errexit = 0;
    
    char *escaped = expand_prompt_escapes(ps1, command_count, extended, 0);
    // fprintf(stderr, "DEBUG: escaped=[%s]\n", escaped ? escaped : "NULL");
    char *expanded;
    
    // Always call expand_word
    expanded = expand_word(escaped ? escaped : ps1);
    // fprintf(stderr, "DEBUG: expanded=[%s]\n", expanded ? expanded : "NULL");
    
    if (escaped) free(escaped);
    
    // Fallback if expansion failed
    if (!expanded) {
        expanded = strdup("$ "); 
    }
    
    // Restore state
    shell_xtrace = saved_xtrace;
    shell_errexit = saved_errexit;
    if (saved_status) {
        shell_var_set("?", saved_status);
        free(saved_status);
    }
    
    // Restore signals
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    
    return expanded;
}
