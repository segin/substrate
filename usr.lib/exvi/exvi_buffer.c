#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void
buf_init(buffer_t *b)
{
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    b->filename = NULL;
    b->modified = 0;
    for (int i = 0; i < 26; i++) {
        b->marks[i] = NULL;
        b->mark_cols[i] = 0;
    }
}

line_t *
buf_insert_after(buffer_t *b, line_t *pos, const char *text)
{
    line_t *l = calloc(1, sizeof(line_t));

    if (!l) {
        return NULL;
    }
    l->text = strdup(text);
    l->len = strlen(text);

    if (!b->head) {
        b->head = b->tail = b->cur = l;
    } else if (!pos) {
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

void
buf_delete(buffer_t *b, line_t *l)
{
    if (!l) {
        return;
    }
    for (int i = 0; i < 26; i++) {
        if (b->marks[i] == l) {
            b->marks[i] = NULL;
            b->mark_cols[i] = 0;
        }
    }
    if (l->prev) {
        l->prev->next = l->next;
    } else {
        b->head = l->next;
    }
    if (l->next) {
        l->next->prev = l->prev;
    } else {
        b->tail = l->prev;
    }

    if (b->cur == l) {
        if (l->next) {
            b->cur = l->next;
        } else if (l->prev) {
            b->cur = l->prev;
        } else {
            b->cur = NULL;
        }
    }

    free(l->text);
    free(l);
    b->line_count--;
    b->modified = 1;
}

void
buf_free(buffer_t *b)
{
    line_t *curr = b->head;

    while (curr) {
        line_t *next = curr->next;

        free(curr->text);
        free(curr);
        curr = next;
    }
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    if (b->filename) {
        free(b->filename);
        b->filename = NULL;
    }
    for (int i = 0; i < 26; i++) {
        b->marks[i] = NULL;
        b->mark_cols[i] = 0;
    }
}

void
buf_copy(buffer_t *dst, buffer_t *src)
{
    line_t *curr = src->head;
    line_t *pos = NULL;

    buf_free(dst);
    if (src->filename) {
        dst->filename = strdup(src->filename);
    }
    dst->modified = src->modified;
    while (curr) {
        pos = buf_insert_after(dst, pos, curr->text);
        curr = curr->next;
    }
}

void
save_undo(buffer_t *current)
{
    buf_copy(&undo_buf, current);
    undo_valid = 1;
}

void
buf_read_file(buffer_t *b, const char *filename)
{
    FILE *f = fopen(filename, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    line_t *pos = b->tail;

    if (!f) {
        return;
    }
    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret - 1] == '\n') {
            line[ret - 1] = '\0';
        }
        pos = buf_insert_after(b, pos, line);
    }
    free(line);
    fclose(f);
    b->cur = b->head;
    b->modified = 0;
}

static int
write_range_to_stream(buffer_t *b, FILE *f, int addr1, int addr2)
{
    line_t *curr;

    if (addr1 == -1 || addr2 == -1) {
        addr1 = 1;
        addr2 = b->line_count;
    }
    if (b->line_count == 0 && addr1 == 1 && addr2 == 0) {
        return 0;
    }
    if (addr1 < 1 || addr2 < addr1 || addr2 > b->line_count) {
        return -1;
    }

    curr = buf_get_line(b, addr1);
    for (int line = addr1; line <= addr2 && curr; line++) {
        fprintf(f, "%s\n", curr->text);
        curr = curr->next;
    }
    return 0;
}

void
buf_write_file(buffer_t *b, const char *filename, int append)
{
    buf_write_range(b, filename, append, 1, b->line_count);
}

void
buf_write_range(buffer_t *b, const char *filename, int append, int addr1, int addr2)
{
    int wrote_current_file = 0;
    int wrote_whole_buffer = 0;

    if (filename[0] == '!') {
        FILE *f;

        if (secure_mode) {
            fprintf(stderr, "Shell commands not allowed in secure mode\n");
            return;
        }
        f = popen(filename + 1, "w");
        if (!f) {
            return;
        }
        if (write_range_to_stream(b, f, addr1, addr2) != 0) {
            pclose(f);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        pclose(f);
        return;
    }

    if (b->filename && strcmp(filename, b->filename) == 0) {
        wrote_current_file = 1;
    }
    if (addr1 == 1 && addr2 == b->line_count) {
        wrote_whole_buffer = 1;
    }

    if (append) {
        FILE *f = fopen(filename, "a");

        if (!f) {
            perror(filename);
            return;
        }
        if (write_range_to_stream(b, f, addr1, addr2) != 0) {
            fclose(f);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        fclose(f);
    } else {
        char *tmp;
        int fd;
        FILE *f;

        if (asprintf(&tmp, "%s.tmp.XXXXXX", filename) < 0) {
            perror("asprintf");
            return;
        }

        fd = mkstemp(tmp);
        if (fd < 0) {
            perror(tmp);
            free(tmp);
            return;
        }

        f = fdopen(fd, "w");
        if (!f) {
            close(fd);
            remove(tmp);
            free(tmp);
            return;
        }

        if (write_range_to_stream(b, f, addr1, addr2) != 0) {
            fclose(f);
            remove(tmp);
            free(tmp);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        fclose(f);
        if (rename(tmp, filename) < 0) {
            perror("rename");
            remove(tmp);
            free(tmp);
            return;
        }
        free(tmp);
    }
    if (wrote_current_file && wrote_whole_buffer && !append) {
        b->modified = 0;
    }
}

line_t *
buf_get_line(buffer_t *b, int line_num)
{
    line_t *l = b->head;

    if (line_num < 1 || line_num > b->line_count) {
        return NULL;
    }
    for (int i = 1; i < line_num && l; i++) {
        l = l->next;
    }
    return l;
}

int
buf_current_line(buffer_t *b)
{
    line_t *l;
    int idx = 1;

    if (!b->cur) {
        return -1;
    }

    l = b->head;
    while (l && l != b->cur) {
        idx++;
        l = l->next;
    }

    return l ? idx : -1;
}
