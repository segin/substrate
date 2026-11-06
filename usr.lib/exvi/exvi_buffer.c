#include "exvi_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int
buf_line_array_grow(buffer_t *b, int needed)
{
    int new_cap;
    line_t **arr;

    if (needed <= b->line_array_cap) {
        return 0;
    }
    new_cap = b->line_array_cap > 0 ? b->line_array_cap : 64;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    arr = realloc(b->line_array, sizeof(*arr) * (size_t)new_cap);
    if (!arr) {
        return -1;
    }
    b->line_array = arr;
    b->line_array_cap = new_cap;
    return 0;
}

static int
buf_line_index(buffer_t *b, line_t *l)
{
    if (!l || !b->line_array) {
        return -1;
    }
    for (int i = 0; i < b->line_count; i++) {
        if (b->line_array[i] == l) {
            return i;
        }
    }
    return -1;
}

static void
exvi_history_free(exvi_history_t *history)
{
    for (int i = 0; i < history->len; i++) {
        buf_free(&history->items[i]);
    }
    free(history->items);
    history->items = NULL;
    history->len = 0;
    history->cap = 0;
}

static int
exvi_history_grow(exvi_history_t *history)
{
    int new_cap = history->cap > 0 ? history->cap * 2 : 8;
    buffer_t *items = realloc(history->items, sizeof(*items) * (size_t)new_cap);

    if (!items) {
        return -1;
    }
    history->items = items;
    history->cap = new_cap;
    return 0;
}

int
exvi_history_push_snapshot(exvi_history_t *history, buffer_t *src)
{
    if (history->len >= history->cap && exvi_history_grow(history) != 0) {
        return -1;
    }
    buf_init(&history->items[history->len]);
    exvi_history_suspended++;
    buf_copy(&history->items[history->len], src);
    exvi_history_suspended--;
    history->len++;
    return 0;
}

static int
exvi_history_push_move(exvi_history_t *history, buffer_t *src)
{
    if (history->len >= history->cap && exvi_history_grow(history) != 0) {
        return -1;
    }
    history->items[history->len++] = *src;
    buf_init(src);
    return 0;
}

int
exvi_history_pop_snapshot(exvi_history_t *history, buffer_t *out)
{
    if (history->len <= 0) {
        return -1;
    }
    *out = history->items[--history->len];
    buf_init(&history->items[history->len]);
    return 0;
}

void
exvi_reset_undo_state(void)
{
    exvi_history_free(&undo_history);
    exvi_history_free(&redo_history);
    buf_free(&pending_undo_buf);
    buf_init(&pending_undo_buf);
    pending_undo_valid = 0;
}

void
exvi_discard_pending_undo(void)
{
    if (!pending_undo_valid) {
        return;
    }
    buf_free(&pending_undo_buf);
    buf_init(&pending_undo_buf);
    pending_undo_valid = 0;
}

unsigned long exvi_buffer_generation = 0;

void
exvi_note_buffer_change(void)
{
    exvi_buffer_generation++;
    if (exvi_history_suspended || !pending_undo_valid) {
        return;
    }
    if (exvi_history_push_move(&undo_history, &pending_undo_buf) != 0) {
        exvi_report_error("out of memory");
        return;
    }
    exvi_history_free(&redo_history);
    pending_undo_valid = 0;
}

void
buf_init(buffer_t *b)
{
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    b->trailing_newline = 0;
    b->filename = NULL;
    b->recover_filename = NULL;
    b->modified = 0;
    b->empty_origin = 0;
    b->started_empty = 0;
    b->line_array = NULL;
    b->line_array_cap = 0;
    for (int i = 0; i < 26; i++) {
        b->marks[i] = NULL;
        b->mark_cols[i] = 0;
    }
}

line_t *
buf_insert_after(buffer_t *b, line_t *pos, const char *text)
{
    line_t *l = calloc(1, sizeof(line_t));
    int inserted_at_end = (b->head == NULL || pos == b->tail);
    int idx;

    if (!l) {
        return NULL;
    }
    l->text = strdup(text);
    if (!l->text) {
        free(l);
        return NULL;
    }
    l->len = strlen(text);
    exvi_note_buffer_change();

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

    /* Update line array before incrementing line_count so that
       buf_line_index only scans the valid (old) entries. */
    if (buf_line_array_grow(b, b->line_count + 1) == 0) {
        if (!pos) {
            idx = 0;
        } else {
            idx = buf_line_index(b, pos) + 1;
        }
        if (idx < b->line_count) {
            memmove(&b->line_array[idx + 1], &b->line_array[idx],
                sizeof(*b->line_array) * (size_t)(b->line_count - idx));
        }
        b->line_array[idx] = l;
    } else {
        /* OOM: discard the array so buf_get_line falls back to linked list. */
        free(b->line_array);
        b->line_array = NULL;
        b->line_array_cap = 0;
    }

    b->line_count++;
    b->empty_origin = 0;
    if (inserted_at_end) {
        b->trailing_newline = 1;
    }
    b->modified = 1;

    return l;
}

void
buf_delete(buffer_t *b, line_t *l)
{
    int deleting_tail;
    int idx;

    if (!l) {
        return;
    }
    exvi_note_buffer_change();
    deleting_tail = (l == b->tail);

    idx = buf_line_index(b, l);

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

    if (idx >= 0 && b->line_array) {
        if (idx < b->line_count) {
            memmove(&b->line_array[idx], &b->line_array[idx + 1],
                sizeof(*b->line_array) * (size_t)(b->line_count - idx));
        }
    }

    if (b->line_count == 0) {
        b->trailing_newline = 0;
    } else if (deleting_tail) {
        b->trailing_newline = 1;
    }
    b->modified = 1;
}

void
buf_free(buffer_t *b)
{
    line_t *curr = b->head;

    exvi_buffer_generation++;
    while (curr) {
        line_t *next = curr->next;

        free(curr->text);
        free(curr);
        curr = next;
    }
    b->head = b->tail = b->cur = NULL;
    b->line_count = 0;
    b->trailing_newline = 0;
    if (b->filename) {
        free(b->filename);
        b->filename = NULL;
    }
    if (b->recover_filename) {
        free(b->recover_filename);
        b->recover_filename = NULL;
    }
    free(b->line_array);
    b->line_array = NULL;
    b->line_array_cap = 0;
    b->empty_origin = 0;
    b->started_empty = 0;
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

    exvi_history_suspended++;
    buf_free(dst);
    if (src->filename) {
        dst->filename = strdup(src->filename);
    }
    if (src->recover_filename) {
        dst->recover_filename = strdup(src->recover_filename);
    }
    dst->modified = src->modified;
    dst->empty_origin = src->empty_origin;
    dst->started_empty = src->started_empty;
    dst->trailing_newline = src->trailing_newline;
    while (curr) {
        pos = buf_insert_after(dst, pos, curr->text);
        curr = curr->next;
    }
    dst->modified = src->modified;
    dst->trailing_newline = src->trailing_newline;
    exvi_history_suspended--;
}

void
save_undo(buffer_t *current)
{
    exvi_discard_pending_undo();
    buf_copy(&pending_undo_buf, current);
    pending_undo_valid = 1;
}

void
buf_read_file(buffer_t *b, const char *filename)
{
    FILE *f = fopen(filename, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    line_t *pos = b->tail;
    int last_had_newline = 0;

    if (!f) {
        return;
    }
    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret - 1] == '\n') {
            line[ret - 1] = '\0';
            last_had_newline = 1;
        } else {
            last_had_newline = 0;
        }
        pos = buf_insert_after(b, pos, line);
    }
    free(line);
    fclose(f);
    if (b->line_count == 0) {
        b->empty_origin = 1;
        b->started_empty = 1;
        b->trailing_newline = 0;
    } else {
        b->empty_origin = 0;
        b->started_empty = 0;
        b->trailing_newline = last_had_newline;
    }
    b->cur = b->head;
    b->modified = 0;
}

/**
 * Write lines addr1..addr2 of b to f (-1, -1: the whole buffer).
 * @return 0; -1 if the range is not valid, nothing written; -2 if a write
 *         failed, errno saying why
 */
static int
write_range_to_stream(buffer_t *b, FILE *f, int addr1, int addr2)
{
    line_t *curr;
    int omit_final_newline;
    int synthetic_empty_with_content;
    int has_nonempty_line = 0;

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

    if (b->empty_origin && b->line_count == 1 && addr1 == 1 && addr2 == 1
        && b->head == b->tail && b->head && b->head->len == 0
        && !b->trailing_newline) {
        return 0;
    }

    for (curr = b->head; curr; curr = curr->next) {
        if (curr->len > 0) {
            has_nonempty_line = 1;
            break;
        }
    }
    synthetic_empty_with_content = b->started_empty
        && b->line_count > 0
        && has_nonempty_line;
    omit_final_newline = (addr2 == b->line_count && !b->trailing_newline
        && !synthetic_empty_with_content);
    curr = buf_get_line(b, addr1);
    for (int line = addr1; line <= addr2 && curr; line++) {
        if (fputs(curr->text, f) == EOF) {
            return -2;
        }
        if (!(omit_final_newline && line == addr2) && fputc('\n', f) == EOF) {
            return -2;
        }
        curr = curr->next;
    }
    return 0;
}

/**
 * Flush and close a stream the buffer was written to, and report the first
 * failure: of the writes (write_errno), the flush or the close.  A full disk
 * or a file size limit shows only here, so a write is not done until this
 * says so.
 * @param f the stream; closed in every case
 * @param name the file name to report
 * @param write_errno errno of a failed write, or 0
 * @return 0 if everything reached the file, -1 (reported) if not
 */
static int
finish_written_stream(FILE *f, const char *name, int write_errno)
{
    int err = write_errno;

    if (!err && fflush(f) != 0) {
        err = errno ? errno : EIO;
    }
    if (fclose(f) != 0 && !err) {
        err = errno ? errno : EIO;
    }
    if (err) {
        fprintf(stderr, "%s: %s\n", name, strerror(err));
        return -1;
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

        if (secure_mode || restricted_mode) {
            exvi_report_shell_forbidden();
            return;
        }
        f = exvi_popen(filename + 1, "w");
        if (!f) {
            return;
        }
        if (write_range_to_stream(b, f, addr1, addr2) != 0) {
            exvi_pclose(f);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        exvi_pclose(f);
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
        int rc;

        if (!f) {
            perror(filename);
            return;
        }
        errno = 0;
        rc = write_range_to_stream(b, f, addr1, addr2);
        if (rc == -1) {
            fclose(f);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        if (finish_written_stream(f, filename, rc == -2 ? (errno ? errno : EIO) : 0) != 0) {
            return;
        }
    } else {
        char *tmp;
        int fd;
        FILE *f;
        int rc;

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

        /*
         * mkstemp() creates the temp file with mode 0600, and the rename()
         * below replaces the original file with it -- so without this the saved
         * file loses its permissions (a 0755 script becomes 0600 and stops
         * being executable).  Carry the original file's mode (and, best effort,
         * its owner) across to the replacement; for a brand-new file fall back
         * to the normal create default, 0666 masked by the umask.
         */
        {
            struct stat st;
            if (stat(filename, &st) == 0) {
                fchmod(fd, st.st_mode & 07777);
                /* Best effort -- only succeeds for root / matching owner. */
                if (fchown(fd, st.st_uid, st.st_gid) != 0) {
                    /* ignore: not privileged to preserve ownership */
                }
            } else {
                mode_t um = umask(0);
                umask(um);
                fchmod(fd, 0666 & ~um);
            }
        }

        f = fdopen(fd, "w");
        if (!f) {
            close(fd);
            remove(tmp);
            free(tmp);
            return;
        }

        errno = 0;
        rc = write_range_to_stream(b, f, addr1, addr2);
        if (rc == -1) {
            fclose(f);
            remove(tmp);
            free(tmp);
            fprintf(stderr, "Invalid write range\n");
            return;
        }
        /* the original is replaced only by a copy known to be whole */
        if (finish_written_stream(f, filename, rc == -2 ? (errno ? errno : EIO) : 0) != 0) {
            remove(tmp);
            free(tmp);
            return;
        }
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
        exvi_cleanup_buffer_recover_file(b);
    }
}

line_t *
buf_get_line(buffer_t *b, int line_num)
{
    if (line_num < 1 || line_num > b->line_count) {
        return NULL;
    }
    if (b->line_array) {
        return b->line_array[line_num - 1];
    }
    {
        line_t *l = b->head;

        for (int i = 1; i < line_num && l; i++) {
            l = l->next;
        }
        return l;
    }
}

int
buf_current_line(buffer_t *b)
{
    if (!b->cur) {
        return -1;
    }
    if (b->line_array) {
        int idx = buf_line_index(b, b->cur);

        return idx >= 0 ? idx + 1 : -1;
    }
    {
        line_t *l = b->head;
        int idx = 1;

        while (l && l != b->cur) {
            idx++;
            l = l->next;
        }
        return l ? idx : -1;
    }
}
