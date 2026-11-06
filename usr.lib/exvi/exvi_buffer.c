#include "exvi_internal.h"

#include <errno.h>
#include <fcntl.h>
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
    b->read_failed = 0;
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
    dst->read_failed = src->read_failed;
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

/**
 * Read filename into b.  A file that does not exist is a new file, and b
 * stays empty; any other failure to open or read it is reported and marks
 * b read_failed, so that the partial buffer is not written back over the
 * file without `!`.
 */
void
buf_read_file(buffer_t *b, const char *filename)
{
    FILE *f = fopen(filename, "r");

    b->read_failed = 0;
    if (!f) {
        if (errno != ENOENT) {
            exvi_report_errorf("%s: %s", filename, strerror(errno));
            b->read_failed = 1;
        }
        return;
    }
    if (buf_read_stream(b, f) != 0) {
        exvi_report_errorf("%s: %s", filename, strerror(errno ? errno : EIO));
        b->read_failed = 1;
    }
    fclose(f);
}

/**
 * Append the lines of f to b, and mark b as just read: unmodified, with the
 * current line its first.  f is left open.
 * @return 0, or -1 if reading stopped on an error (errno says which)
 */
int
buf_read_stream(buffer_t *b, FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    line_t *pos = b->tail;
    int last_had_newline = 0;
    int failed;

    errno = 0;
    while ((ret = getline(&line, &cap, f)) != -1) {
        if (ret > 0 && line[ret - 1] == '\n') {
            line[ret - 1] = '\0';
            last_had_newline = 1;
        } else {
            last_had_newline = 0;
        }
        pos = buf_insert_after(b, pos, line);
    }
    failed = ferror(f);
    {
        int saved_errno = errno;

        free(line);
        errno = saved_errno;
    }
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
    return failed ? -1 : 0;
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
    /* on stable storage before it is reported written (EINVAL: no such thing here) */
    if (!err && fsync(fileno(f)) != 0 && errno != EINVAL) {
        err = errno;
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

/**
 * Write lines addr1..addr2 of b to path, a file that does not exist yet:
 * created with the usual 0666 less the umask, and removed again if the
 * write fails.
 * @return 0, or -1 (reported)
 */
static int
write_new_file(buffer_t *b, const char *path, int addr1, int addr2)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    FILE *f;
    int rc;

    if (fd < 0) {
        perror(path);
        return -1;
    }
    f = fdopen(fd, "w");
    if (!f) {
        perror(path);
        close(fd);
        unlink(path);
        return -1;
    }
    errno = 0;
    rc = write_range_to_stream(b, f, addr1, addr2);
    if (rc == -1) {
        fclose(f);
        unlink(path);
        fprintf(stderr, "Invalid write range\n");
        return -1;
    }
    if (finish_written_stream(f, path, rc == -2 ? (errno ? errno : EIO) : 0) != 0) {
        unlink(path);
        return -1;
    }
    return 0;
}

/**
 * Write lines addr1..addr2 of b over path through a temporary file renamed
 * over it, which no failure can leave half written.  Only where that is
 * the same as writing the file itself: the caller has checked that it is a
 * regular file with one link, owned by the user; here the temporary file
 * must also take the file's group (fchown before fchmod, which would clear
 * set-user-ID and set-group-ID).
 * @return 0; -1 (reported) if the write failed and path is unchanged; 1 if
 *         this way cannot be taken (no temporary file beside it, or its
 *         group), with nothing done
 */
static int
write_by_rename(buffer_t *b, const char *path, const struct stat *st, int addr1, int addr2)
{
    char *tmp;
    int fd;
    FILE *f;
    int rc;

    if (asprintf(&tmp, "%s.tmp.XXXXXX", path) < 0) {
        return 1;
    }
    fd = mkstemp(tmp);
    if (fd < 0) {
        free(tmp);
        return 1;
    }
    if (fchown(fd, (uid_t)-1, st->st_gid) != 0) {
        close(fd);
        unlink(tmp);
        free(tmp);
        return 1;
    }
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        free(tmp);
        return 1;
    }
    errno = 0;
    rc = write_range_to_stream(b, f, addr1, addr2);
    if (rc == -1) {
        fclose(f);
        unlink(tmp);
        free(tmp);
        fprintf(stderr, "Invalid write range\n");
        return -1;
    }
    /*
     * The mode once the data is written: a write clears set-user-ID and
     * set-group-ID, and fchown (above) would too.
     */
    if (rc == 0 && (fflush(f) != 0 || fchmod(fd, st->st_mode & 07777) != 0)) {
        rc = -2;
    }
    /* the original is replaced only by a copy known to be whole */
    if (finish_written_stream(f, path, rc == -2 ? (errno ? errno : EIO) : 0) != 0) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
    if (rename(tmp, path) < 0) {
        perror(path);
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

/**
 * Write lines addr1..addr2 of b into path itself, keeping its inode, links,
 * owner and mode.  What it held is read first and put back if the write
 * fails; a file that cannot be read is written without that.
 * @return 0, or -1 (reported)
 */
static int
write_in_place(buffer_t *b, const char *path, int addr1, int addr2)
{
    char *orig = NULL;
    size_t orig_len = 0;
    size_t orig_cap = 0;
    int fd = open(path, O_RDWR);
    FILE *f;
    int rc;
    int err = 0;
    mode_t orig_mode = 0;
    struct stat ost;

    if (stat(path, &ost) == 0) {
        orig_mode = ost.st_mode;
    }
    if (fd >= 0) {
        for (;;) {
            ssize_t n;

            if (orig_len == orig_cap) {
                size_t cap = orig_cap ? orig_cap * 2 : 65536;
                char *grown = realloc(orig, cap);

                if (!grown) {
                    free(orig);
                    close(fd);
                    exvi_report_error("out of memory");
                    return -1;
                }
                orig = grown;
                orig_cap = cap;
            }
            n = read(fd, orig + orig_len, orig_cap - orig_len);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0) {
                err = errno;
                break;
            }
            if (n == 0) {
                break;
            }
            orig_len += (size_t)n;
        }
        if (err || lseek(fd, 0, SEEK_SET) != 0 || ftruncate(fd, 0) != 0) {
            perror(path);
            free(orig);
            close(fd);
            return -1;
        }
    } else {
        /* written but not readable: nothing to put back */
        fd = open(path, O_WRONLY | O_TRUNC);
        if (fd < 0) {
            perror(path);
            return -1;
        }
    }
    f = fdopen(fd, "w");
    if (!f) {
        perror(path);
        close(fd);
        free(orig);
        return -1;
    }
    errno = 0;
    rc = write_range_to_stream(b, f, addr1, addr2);
    if (rc == -2) {
        err = errno ? errno : EIO;
    } else if (rc == 0 && fflush(f) != 0) {
        err = errno ? errno : EIO;
    } else if (rc == 0 && fsync(fd) != 0 && errno != EINVAL) {
        err = errno;
    }
    if (rc == 0 && !err) {
        struct stat st;

        /* a write clears set-user-ID and set-group-ID: the owner may set them back */
        if (fstat(fd, &st) == 0 && st.st_uid == geteuid()) {
            (void)fchmod(fd, orig_mode & 07777);
        }
    }
    if (rc == -1 || err) {
        if (orig) {
            /* put back what the file held */
            size_t off = 0;

            (void)fflush(f);
            if (lseek(fd, 0, SEEK_SET) == 0 && ftruncate(fd, 0) == 0) {
                while (off < orig_len) {
                    ssize_t n = write(fd, orig + off, orig_len - off);

                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    if (n <= 0) {
                        break;
                    }
                    off += (size_t)n;
                }
            }
        }
        fclose(f);
        free(orig);
        if (rc == -1) {
            fprintf(stderr, "Invalid write range\n");
        } else {
            fprintf(stderr, "%s: %s\n", path, strerror(err));
        }
        return -1;
    }
    free(orig);
    if (fclose(f) != 0) {
        perror(path);
        return -1;
    }
    return 0;
}

/**
 * Write lines addr1..addr2 of b to filename, replacing what it holds.  A
 * symbolic link is followed: its target is written.  A file that does not
 * exist is created.  An existing file is written through a temporary file
 * renamed over it where that is the same as writing it -- a regular file
 * with one link, the user's own -- since no failure can leave that half
 * written; otherwise it is written in place, keeping its inode, links,
 * owner and mode, with what it held put back if the write fails.
 * @return 0, or -1 (reported; the file as it was)
 */
static int
write_file_whole(buffer_t *b, const char *filename, int addr1, int addr2)
{
    char *resolved = NULL;
    const char *path = filename;
    struct stat st;
    int rc;

    if (lstat(filename, &st) == 0 && S_ISLNK(st.st_mode)) {
        resolved = realpath(filename, NULL);
        if (!resolved) {
            /* a dangling link: create its target */
            char target[4096];
            ssize_t n = readlink(filename, target, sizeof(target) - 1);

            if (n < 0) {
                perror(filename);
                return -1;
            }
            target[n] = '\0';
            if (target[0] != '/' && strchr(filename, '/')) {
                const char *slash = strrchr(filename, '/');

                if (asprintf(&resolved, "%.*s/%s", (int)(slash - filename), filename, target) < 0) {
                    return -1;
                }
            } else {
                resolved = strdup(target);
                if (!resolved) {
                    return -1;
                }
            }
        }
        path = resolved;
    }
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            rc = write_new_file(b, path, addr1, addr2);
        } else {
            perror(path);
            rc = -1;
        }
        free(resolved);
        return rc;
    }
    rc = 1;
    /* (and writable: a rename would replace a file the user may not write) */
    if (S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_uid == geteuid()
            && access(path, W_OK) == 0) {
        rc = write_by_rename(b, path, &st, addr1, addr2);
    }
    if (rc == 1) {
        rc = write_in_place(b, path, addr1, addr2);
    }
    free(resolved);
    return rc;
}

int
buf_write_file(buffer_t *b, const char *filename, int append)
{
    return buf_write_range(b, filename, append, 1, b->line_count);
}

/**
 * Write lines addr1..addr2 of b to filename (appending if append), or to
 * the command after a leading `!`.
 * @return 0 if it was written, -1 (reported) if not
 */
int
buf_write_range(buffer_t *b, const char *filename, int append, int addr1, int addr2)
{
    int wrote_current_file = 0;
    int wrote_whole_buffer = 0;

    if (filename[0] == '!') {
        FILE *f;

        if (secure_mode || restricted_mode) {
            exvi_report_shell_forbidden();
            return -1;
        }
        f = exvi_popen(filename + 1, "w");
        if (!f) {
            return -1;
        }
        if (write_range_to_stream(b, f, addr1, addr2) != 0) {
            exvi_pclose(f);
            fprintf(stderr, "Invalid write range\n");
            return -1;
        }
        exvi_pclose(f);
        return 0;
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
            return -1;
        }
        errno = 0;
        rc = write_range_to_stream(b, f, addr1, addr2);
        if (rc == -1) {
            fclose(f);
            fprintf(stderr, "Invalid write range\n");
            return -1;
        }
        if (finish_written_stream(f, filename, rc == -2 ? (errno ? errno : EIO) : 0) != 0) {
            return -1;
        }
    } else if (write_file_whole(b, filename, addr1, addr2) != 0) {
        return -1;
    }
    if (wrote_current_file && wrote_whole_buffer && !append) {
        b->modified = 0;
        b->read_failed = 0;
        exvi_cleanup_buffer_recover_file(b);
    }
    return 0;
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
