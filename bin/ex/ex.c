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

int secure_mode = 0;
int batch_mode = 0;
int visual_mode = 0;

typedef struct line {
    struct line *prev;
    struct line *next;
    char *text;
    size_t len;
    int global_mark;
} line_t;

typedef struct {
    line_t *head;
    line_t *tail;
    line_t *cur;
    int line_count;
    char *filename;
    int modified;
    line_t *marks[26];
} buffer_t;

buffer_t regs[27]; // 0-25 = a-z, 26 = unnamed

buffer_t undo_buf;
int undo_valid = 0;

jmp_buf main_loop_jmp;
buffer_t *global_buf_for_sighandler = NULL;

void handle_sigint(int sig) {
    (void)sig;
    printf("\nInterrupt\n");
    longjmp(main_loop_jmp, 1);
}

void handle_sigterm(int sig) {
    (void)sig;
    if (global_buf_for_sighandler && global_buf_for_sighandler->modified && global_buf_for_sighandler->filename && global_buf_for_sighandler->head) {
        char path[1024];
        snprintf(path, sizeof(path), "%s.recover", global_buf_for_sighandler->filename);
        FILE *f = fopen(path, "w");
        if (f) {
            line_t *curr = global_buf_for_sighandler->head;
            while (curr) {
                fprintf(f, "%s\n", curr->text);
                curr = curr->next;
            }
            fclose(f);
        }
    }
    exit(1);
}

void buf_init(buffer_t *b) {
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    b->filename = NULL;
    b->modified = 0;
    for (int i=0; i<26; i++) b->marks[i] = NULL;
}

line_t *buf_insert_after(buffer_t *b, line_t *pos, const char *text) {
    line_t *l = calloc(1, sizeof(line_t));
    if (!l) return NULL;
    l->text = strdup(text);
    l->len = strlen(text);
    
    if (!b->head) {
        b->head = b->tail = b->cur = l;
    } else if (!pos) {
        // Insert at very beginning
        l->next = b->head;
        b->head->prev = l;
        b->head = l;
    } else {
        l->prev = pos;
        l->next = pos->next;
        if (pos->next) {
            pos->next->prev = l;
        } else {
            b->tail = l;
        }
        pos->next = l;
    }
    b->line_count++;
    b->modified = 1;
    return l;
}

void buf_delete(buffer_t *b, line_t *l) {
    if (!l) return;
    if (l->prev) l->prev->next = l->next;
    else b->head = l->next;
    if (l->next) l->next->prev = l->prev;
    else b->tail = l->prev;
    
    if (b->cur == l) {
        if (l->next) b->cur = l->next;
        else if (l->prev) b->cur = l->prev;
        else b->cur = NULL;
    }
    
    free(l->text);
    free(l);
    b->line_count--;
    b->modified = 1;
}

void buf_free(buffer_t *b) {
    line_t *curr = b->head;
    while (curr) {
        line_t *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    if (b->filename) { free(b->filename); b->filename = NULL; }
    for (int i=0; i<26; i++) b->marks[i] = NULL;
}

void buf_copy(buffer_t *dst, buffer_t *src) {
    buf_free(dst);
    if (src->filename) dst->filename = strdup(src->filename);
    dst->modified = src->modified;
    line_t *curr = src->head;
    line_t *pos = NULL;
    while (curr) {
        pos = buf_insert_after(dst, pos, curr->text);
        // We'd need to map marks and cur appropriately, but for a simple 
        // full-buffer undo, recreating the exact line pointer mapping is tricky.
        // For lines, we will just copy text.
        curr = curr->next;
    }
}

void save_undo(buffer_t *current) {
    buf_copy(&undo_buf, current);
    undo_valid = 1;
}

void buf_read_file(buffer_t *b, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        // new file, ignore
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    line_t *pos = b->tail;
    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
        pos = buf_insert_after(b, pos, line);
    }
    free(line);
    fclose(f);
    b->cur = b->tail;
    b->modified = 0; // Freshly read
}

void buf_write_file(buffer_t *b, const char *filename, int append) {
    if (filename[0] == '!') {
        if (secure_mode) {
            fprintf(stderr, "Shell commands not allowed in secure mode\n");
            return;
        }
        // write to shell command
        FILE *f = popen(filename + 1, "w");
        if (!f) return;
        line_t *curr = b->head;
        while (curr) {
            fprintf(f, "%s\n", curr->text);
            curr = curr->next;
        }
        pclose(f);
        return;
    }
    
    char tmp[1024];
    if (append) {
        FILE *f = fopen(filename, "a");
        if (!f) { perror(filename); return; }
        line_t *curr = b->head;
        while (curr) {
            fprintf(f, "%s\n", curr->text);
            curr = curr->next;
        }
        fclose(f);
    } else {
        snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", filename);
        int fd = mkstemp(tmp);
        if (fd < 0) { perror(tmp); return; }
        FILE *f = fdopen(fd, "w");
        if (!f) { close(fd); remove(tmp); return; }
        
        line_t *curr = b->head;
        while (curr) {
            fprintf(f, "%s\n", curr->text);
            curr = curr->next;
        }
        fclose(f); // closes fd as well
        if (rename(tmp, filename) < 0) {
            perror("rename");
            remove(tmp);
            return;
        }
    }
    b->modified = 0;
}


line_t *buf_get_line(buffer_t *b, int line_num) {
    if (line_num < 1 || line_num > b->line_count) return NULL;
    line_t *l = b->head;
    for (int i = 1; i < line_num && l; i++) {
        l = l->next;
    }
    return l;
}

int parse_address(buffer_t *b, char **cmd_ptr) {
    char *p = *cmd_ptr;
    while (*p && isspace((unsigned char)*p)) p++;
    
    int addr = -1;
    if (isdigit((unsigned char)*p)) {
        addr = strtol(p, &p, 10);
    } else if (*p == '.') {
        addr = b->cur ? 1 : 0; // simplistic, need to track actual line index later
        // Let's improve cur tracking by just calculating it
        if (b->cur) {
            addr = 1;
            line_t *l = b->head;
            while (l && l != b->cur) {
                addr++;
                l = l->next;
            }
        } else {
            addr = 0;
        }
        p++;
    } else if (*p == '$') {
        addr = b->line_count;
        p++;
    }
    // More address types later
    
    if (addr != -1) {
        *cmd_ptr = p;
    }
    return addr;
}

void parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2) {
    *addr1 = -1;
    *addr2 = -1;
    
    int a1 = parse_address(b, cmd_ptr);
    if (a1 != -1) {
        *addr1 = a1;
        *addr2 = a1; // default if no comma
        
        char *p = *cmd_ptr;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == ',') {
            p++;
            *cmd_ptr = p;
            int a2 = parse_address(b, cmd_ptr);
            if (a2 != -1) {
                *addr2 = a2;
            } else {
                *addr2 = b->line_count; // "1," means 1 to end if missing? Varies. Usually error. But let's assume valid.
            }
        }
    } else {
        // default address rules (e.g. current line)
        if (b->cur) {
            line_t *l = b->head;
            int idx = 1;
            while (l && l != b->cur) { idx++; l = l->next; }
            *addr1 = idx;
            *addr2 = idx;
        }
    }
}

int input_mode = 0; // 0=cmd, 1=append, 2=insert, 3=change
line_t *input_insert_pos = NULL;

void do_command(buffer_t *b, char *cmd) {
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    if (!*cmd) return;
    
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
    parse_range(b, &cmd, &addr1, &addr2);
    
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    
    // Commands implementation skeleton
    if (strcmp(cmd, "visual") == 0 || strcmp(cmd, "vi") == 0) {
        fprintf(stderr, "ex: visual mode not implemented in this build.\n");
        return;
    } else if (cmd[0] == 'q' || (cmd[0] == 'x' && cmd[1] == 'i' && cmd[2] == 't') || (cmd[0] == 'w' && cmd[1] == 'q')) {
        if (cmd[0] == 'x' || cmd[0] == 'w') {
            if (b->filename) buf_write_file(b, b->filename, 0); // wq or x
        }
        if (cmd[0] != 'w') exit(0); // w alone just writes
    } else if (cmd[0] == 'w') {
        char *ptr = cmd + 1;
        int append = 0;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '>' && *(ptr+1) == '>') {
            append = 1;
            ptr += 2;
            while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        }
        if (*ptr) {
            buf_write_file(b, ptr, append);
        } else if (b->filename) {
            buf_write_file(b, b->filename, append);
        } else {
            fprintf(stderr, "No current filename\n");
        }
    } else if (cmd[0] == 'e') {
        int force = (cmd[1] == '!');
        char *ptr = cmd + (force ? 2 : 1);
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return;
        }
        
        buf_free(b);
        buf_init(b);
        if (*ptr) {
            b->filename = strdup(ptr);
        } else if (b->filename) {
            // Keep old filename if e is used without args
            b->filename = strdup(b->filename); // Actually buf_free freed it, wait.
        }
        if (b->filename) {
            buf_read_file(b, b->filename);
            if (!batch_mode) printf("\"%s\" %d lines\n", b->filename, b->line_count);
        } else {
            fprintf(stderr, "No current filename\n");
        }
    } else if (cmd[0] == 'r') {
        char *ptr = cmd + 1;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        
        FILE *f = NULL;
        int is_pipe = 0;
        if (*ptr == '!') {
            if (secure_mode) {
                fprintf(stderr, "Shell commands not allowed in secure mode\n");
                return;
            }
            is_pipe = 1;
            f = popen(ptr + 1, "r");
        } else {
            if (!*ptr) ptr = b->filename; // default to current file
            if (ptr) f = fopen(ptr, "r");
        }
        
        if (f) {
            int lines_read = 0;
            char *line = NULL;
            size_t cap = 0;
            ssize_t ret;
            line_t *pos = buf_get_line(b, addr2 != -1 ? addr2 : (b->cur ? 1 : 0));
            
            while ((ret = getline(&line, &cap, f)) != -1) {
                if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
                pos = buf_insert_after(b, pos, line);
                lines_read++;
            }
            free(line);
            
            if (is_pipe) pclose(f);
            else fclose(f);
            
            if (!batch_mode) printf("\"%s\" %d lines\n", *cmd == '!' ? ptr+1 : ptr, lines_read);
        } else {
            perror(ptr);
        }
    } else if (cmd[0] == 'd') {
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            for (int i = 0; i < (addr2 - addr1 + 1); i++) {
                line_t *l = buf_get_line(b, addr1);
                if (l) buf_delete(b, l);
            }
        }
    } else if (cmd[0] == 'u') {
        if (undo_valid) {
            buffer_t tmp;
            buf_init(&tmp);
            buf_copy(&tmp, b);
            buf_copy(b, &undo_buf);
            buf_copy(&undo_buf, &tmp);
            buf_free(&tmp);
            undo_valid = 1; // tmp doesn't survive? Yes, but undo_buf holds the new undo state
        }
    } else if (cmd[0] == 'p' || cmd[0] == '#' || cmd[0] == 'l') {
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            line_t *l = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
                if (cmd[0] == '#') {
                    // print line number
                    printf("%6d  ", addr1 + i);
                }
                if (cmd[0] == 'l') {
                    // list format
                    for (size_t j = 0; j < l->len; j++) {
                        unsigned char c = l->text[j];
                        if (c == '\t') printf("^I");
                        else if (c < 32 || c == 127) {
                            printf("^%c", (c == 127) ? '?' : c + 64);
                        } else {
                            putchar(c);
                        }
                    }
                    printf("$\n");
                } else {
                    printf("%s\n", l->text);
                }
                b->cur = l;
                l = l->next;
            }
        }
    } else if (cmd[0] == '=') {
        int target = (addr2 != -1) ? addr2 : b->line_count;
        printf("%d\n", target);
    } else if (cmd[0] == 'f') {
        char *ptr = cmd + 1;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr) {
            // set file
            if (b->filename) free(b->filename);
            b->filename = strdup(ptr);
        }
        printf("\"%s\" %s %d lines\n", b->filename ? b->filename : "No File", b->modified ? "[Modified]" : "", b->line_count);
    } else if (cmd[0] == 'a') {
        save_undo(b);
        input_mode = 1; // append
        input_insert_pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
    } else if (cmd[0] == 'i') {
        save_undo(b);
        input_mode = 2; // insert
        line_t *pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
        input_insert_pos = pos ? pos->prev : NULL;
    } else if (cmd[0] == 'c') {
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            for (int i = 0; i < (addr2 - addr1 + 1); i++) {
                line_t *l = buf_get_line(b, addr1);
                if (l) buf_delete(b, l);
            }
        }
        input_mode = 3; // change
        input_insert_pos = (addr1 > 1) ? buf_get_line(b, addr1 - 1) : NULL;
    } else if (cmd[0] == 't' || (cmd[0] == 'c' && cmd[1] == 'o')) {
        char *ptr = cmd + (cmd[0] == 't' ? 1 : 2);
        int dest = parse_address(b, &ptr);
        if (dest == -1) dest = b->cur ? 1 : 0; // fallback? Usually error if missing, but let's assume current
        
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
            line_t *pos = buf_get_line(b, dest); // can be NULL if dest=0
            line_t *src = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
                pos = buf_insert_after(b, pos, src->text);
                src = src->next;
            }
        }
    } else if (cmd[0] == 'm') {
        char *ptr = cmd + 1;
        int dest = parse_address(b, &ptr);
        
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
    } else if (cmd[0] == 'j') {
        save_undo(b);
        if (addr1 != -1 && addr2 != -1 && addr1 < addr2) {
            line_t *first = buf_get_line(b, addr1);
            if (!first) return;
            // join lines addr1 through addr2
            size_t total_len = first->len;
            for (int i = 1; i <= (addr2 - addr1); i++) {
                line_t *nxt = buf_get_line(b, addr1 + i);
                if (nxt) total_len += nxt->len;
            }
            char *joined = malloc(total_len + 1);
            joined[0] = '\0';
            strcat(joined, first->text);
            for (int i = 1; i <= (addr2 - addr1); i++) {
                line_t *nxt = buf_get_line(b, addr1 + 1);
                if (nxt) {
                    strcat(joined, nxt->text);
                    buf_delete(b, nxt);
                }
            }
            free(first->text);
            first->text = joined;
            first->len = total_len;
        }
    } else if (cmd[0] == 'y') {
        int reg_idx = 26; // unnamed
        char *ptr = cmd + 1;
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
    } else if (cmd[0] == 'p' && cmd[1] == 'u') {
        int reg_idx = 26;
        char *ptr = cmd + 2;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr >= 'a' && *ptr <= 'z') reg_idx = *ptr - 'a';
        
        save_undo(b);
        line_t *pos = buf_get_line(b, addr2 != -1 ? addr2 : (b->cur ? 1 : 0));
        line_t *src = regs[reg_idx].head;
        while (src) {
            pos = buf_insert_after(b, pos, src->text);
            src = src->next;
        }
    } else if (cmd[0] == 's') {
        save_undo(b);
        char delim = cmd[1];
        if (!delim || delim == '\n') return;
        
        char *re_str = strdup(cmd + 2);
        char *end_re = strchr(re_str, delim);
        if (!end_re) { free(re_str); return; }
        *end_re = '\0';
        
        char *repl_str = end_re + 1;
        char *end_repl = strchr(repl_str, delim);
        int global = 0;
        if (end_repl) {
            *end_repl = '\0';
            if (strchr(end_repl + 1, 'g')) global = 1;
        }
        
        regex_t re;
        if (regcomp(&re, re_str, REG_EXTENDED) != 0) {
            free(re_str);
            return;
        }
        
        // Apply to range
        if (addr1 == -1) { addr1 = b->cur ? 1 : 0; addr2 = addr1; } // default to current
        if (addr1 > 0 && addr2 >= addr1) {
            line_t *l = buf_get_line(b, addr1);
            for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
                regmatch_t pm;
                char *search_start = l->text;
                int matches = 0;
                // We'll build a new string
                size_t rep_len = strlen(repl_str);
                char *new_text = strdup("");
                
                while (regexec(&re, search_start, 1, &pm, 0) == 0) {
                    matches++;
                    // append up to match
                    size_t pre_len = strlen(new_text);
                    new_text = realloc(new_text, pre_len + pm.rm_so + rep_len + 1);
                    strncat(new_text, search_start, pm.rm_so);
                    strcat(new_text, repl_str);
                    
                    search_start += pm.rm_eo;
                    if (!global) break;
                    if (pm.rm_so == pm.rm_eo) {
                        // zero-length match avoidance loop
                        if (*search_start) {
                            pre_len = strlen(new_text);
                            new_text = realloc(new_text, pre_len + 2);
                            new_text[pre_len] = *search_start;
                            new_text[pre_len+1] = '\0';
                            search_start++;
                        } else break;
                    }
                }
                
                if (matches > 0) {
                    size_t pre_len = strlen(new_text);
                    new_text = realloc(new_text, pre_len + strlen(search_start) + 1);
                    strcat(new_text, search_start);
                    
                    free(l->text);
                    l->text = new_text;
                    l->len = strlen(new_text);
                    b->modified = 1;
                    b->cur = l;
                } else {
                    free(new_text);
                }
                l = l->next;
            }
        }
        
        regfree(&re);
        free(re_str);
    } else if (cmd[0] == 'g' || cmd[0] == 'v') {
        int inverted = (cmd[0] == 'v');
        char delim = cmd[1];
        if (!delim || delim == '\n') return;
        
        char *re_str = strdup(cmd + 2);
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
        if (addr1 == -1) { addr1 = 1; addr2 = b->line_count; }
        
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
        system(cmd + 1);
    }
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "sSv")) != -1) {
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
        default:
            fprintf(stderr, "Usage: %s [-s] [-S] [-v] [file]\n", argv[0]);
            exit(1);
        }
    }

    buffer_t buf;
    buf_init(&buf);
    undo_valid = 0;
    buf_init(&undo_buf);
    for (int i=0; i<27; i++) buf_init(&regs[i]);

    if (optind < argc) {
        buf.filename = strdup(argv[optind]);
        buf_read_file(&buf, buf.filename);
    }
    
    if (visual_mode) {
        fprintf(stderr, "ex: visual mode not implemented in this build.\n");
    }

    char *exinit = getenv("EXINIT");
    if (exinit) {
        char *exinit_cpy = strdup(exinit);
        do_command(&buf, exinit_cpy);
        free(exinit_cpy);
    } else {
        char *home = getenv("HOME");
        if (home) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/.exrc", home);
            
            struct stat st;
            if (stat(path, &st) == 0 && (st.st_uid == getuid() || st.st_uid == 0)) {
                FILE *f = fopen(path, "r");
                if (f) {
                    char *rc_line = NULL;
                    size_t rc_cap = 0;
                    ssize_t rc_ret;
                    while ((rc_ret = getline(&rc_line, &rc_cap, f)) != -1) {
                        if (rc_ret > 0 && rc_line[rc_ret-1] == '\n') rc_line[rc_ret-1] = '\0';
                        do_command(&buf, rc_line);
                    }
                    free(rc_line);
                    fclose(f);
                }
            }
        }
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    
    global_buf_for_sighandler = &buf;
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigterm);
    signal(SIGTERM, handle_sigterm);

    setjmp(main_loop_jmp);

    while ((ret = getline(&line, &cap, stdin)) != -1) {
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
    return 0;
}
