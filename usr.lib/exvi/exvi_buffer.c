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

/*
 * The index of @p l in the line array.  The line last found, the lines
 * beside it and the last line are tried first: loading appends after the
 * line just added, and :g and most commands work down the buffer.  Every
 * call scanned from the top, which made loading a file, and :g, quadratic
 * (100,000 lines took 1.9 s to load).
 */
static int
buf_line_index(buffer_t *b, line_t *l)
{
    int n = b->line_count;

    if (!l || !b->line_array) {
        return -1;
    }
    for (int i = b->index_hint - 1; i <= b->index_hint + 1; i++) {
        if (i >= 0 && i < n && b->line_array[i] == l) {
            b->index_hint = i;
            return i;
        }
    }
    if (n > 0 && b->line_array[n - 1] == l) {
        b->index_hint = n - 1;
        return n - 1;
    }
    for (int i = 0; i < n; i++) {
        if (b->line_array[i] == l) {
            b->index_hint = i;
            return i;
        }
    }
    return -1;
}

static void
exvi_history_free(exvi_history_t *history)
{
    for (int i = 0; i < history->len; i++) {
        buf_free(&history->items[i].lines);
    }
    free(history->items);
    history->items = NULL;
    history->len = 0;
    history->cap = 0;
}

/* item onto history; its lines are moved, not copied */
static int
exvi_history_push(exvi_history_t *history, exvi_undo_item_t *item)
{
    if (history->len >= history->cap) {
        int new_cap = history->cap > 0 ? history->cap * 2 : 8;
        exvi_undo_item_t *items = realloc(history->items, sizeof(*items) * (size_t)new_cap);

        if (!items) {
            return -1;
        }
        history->items = items;
        history->cap = new_cap;
    }
    history->items[history->len++] = *item;
    buf_init(&item->lines);
    return 0;
}

static int
exvi_lines_same(const line_t *a, const line_t *b)
{
    return a->id == b->id && a->len == b->len && memcmp(a->text, b->text, a->len) == 0;
}

/*
 * The newest undo record, if it still holds the whole buffer as it was:
 * only the lines the change replaced, now that @p b holds what the change
 * made -- the lines both have, the same line with the same text, at the
 * start and at the end are left out.
 */
static void
exvi_compact_undo(buffer_t *b)
{
    exvi_undo_item_t *item;
    buffer_t *before;
    buffer_t old;
    line_t *lb;
    line_t *la;
    line_t *pos = NULL;
    int prefix = 0;
    int suffix = 0;
    int i;

    if (undo_history.len <= 0 || !undo_history.items[undo_history.len - 1].full) {
        return;
    }
    item = &undo_history.items[undo_history.len - 1];
    before = &item->lines;
    for (lb = before->head, la = b->head; lb && la && exvi_lines_same(lb, la);
        lb = lb->next, la = la->next) {
        prefix++;
    }
    for (lb = before->tail, la = b->tail;
        lb && la && suffix < before->line_count - prefix && suffix < b->line_count - prefix &&
        exvi_lines_same(lb, la); lb = lb->prev, la = la->prev) {
        suffix++;
    }
    exvi_history_suspended++;
    buf_init(&old);
    lb = before->head;
    for (i = 0; i < prefix && lb; i++) {
        lb = lb->next;
    }
    for (i = prefix; i < before->line_count - suffix && lb; i++, lb = lb->next) {
        pos = buf_insert_after_n(&old, pos, lb->text, lb->len);
        if (pos) {
            pos->id = lb->id;
        }
    }
    exvi_history_suspended--;
    item->start = prefix;
    item->new_count = b->line_count - prefix - suffix;
    item->trailing_newline = before->trailing_newline;
    buf_free(before);
    item->lines = old;
    item->full = 0;
}

/*
 * Apply @p item to @p b: the new_count lines from line start give way to
 * the item's lines.  What gives way goes into @p inverse, which applied
 * after puts @p b back.  The cursor goes to the first line put back, as
 * Vim's u does; it went to line 1.
 */
static void
exvi_apply_undo_item(buffer_t *b, exvi_undo_item_t *item, exvi_undo_item_t *inverse)
{
    line_t *pos = item->start > 0 ? buf_get_line(b, item->start) : NULL;
    line_t *ipos = NULL;
    line_t *first = NULL;

    exvi_history_suspended++;
    buf_init(&inverse->lines);
    inverse->full = 0;
    inverse->start = item->start;
    inverse->new_count = item->lines.line_count;
    inverse->trailing_newline = b->trailing_newline;
    for (int k = 0; k < item->new_count; k++) {
        line_t *l = pos ? pos->next : b->head;

        if (!l) {
            break;
        }
        ipos = buf_insert_after_n(&inverse->lines, ipos, l->text, l->len);
        if (ipos) {
            ipos->id = l->id;
        }
        b->cur = l;
        buf_delete(b, l);
    }
    ipos = pos;
    for (line_t *o = item->lines.head; o; o = o->next) {
        ipos = buf_insert_after_n(b, ipos, o->text, o->len);
        if (ipos) {
            ipos->id = o->id;
            if (!first) {
                first = ipos;
            }
        }
    }
    b->trailing_newline = item->trailing_newline;
    b->modified = 1;
    if (first) {
        b->cur = first;
    } else if (pos && pos->next) {
        b->cur = pos->next;
    } else if (pos) {
        b->cur = pos;
    } else {
        b->cur = b->head;
    }
    buf_free(&item->lines);
    exvi_history_suspended--;
    exvi_buffer_generation++;
}

/*
 * u (from undo_history to redo_history) or redo (the other way): the
 * newest record applied to @p b, and its inverse kept for the other.  -1
 * when there is none.
 */
int
exvi_history_step(buffer_t *b, exvi_history_t *from, exvi_history_t *to)
{
    exvi_undo_item_t item;
    exvi_undo_item_t inverse;

    exvi_compact_undo(b);
    if (from->len <= 0) {
        return -1;
    }
    item = from->items[--from->len];
    exvi_apply_undo_item(b, &item, &inverse);
    if (exvi_history_push(to, &inverse) != 0) {
        buf_free(&inverse.lines);
        exvi_report_error("out of memory");
    }
    return 0;
}

/* while above zero, the commands run are one change for undo: see exvi_begin_undo_group() */
static int undo_group_depth = 0;

void
exvi_reset_undo_state(void)
{
    undo_group_depth = 0;
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
    {
        exvi_undo_item_t item = { .lines = pending_undo_buf, .full = 1 };

        /* the whole buffer as it was, until exvi_compact_undo() */
        buf_init(&pending_undo_buf);
        if (exvi_history_push(&undo_history, &item) != 0) {
            buf_free(&item.lines);
            exvi_report_error("out of memory");
            pending_undo_valid = 0;
            return;
        }
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
    b->index_hint = 0;
    for (int i = 0; i < 26; i++) {
        b->marks[i] = NULL;
        b->mark_cols[i] = 0;
    }
}

line_t *
buf_insert_after(buffer_t *b, line_t *pos, const char *text)
{
    return buf_insert_after_n(b, pos, text, strlen(text));
}

/**
 * Insert a line of the @p len bytes at @p text after @p pos (NULL: first).
 * The bytes may hold a NUL, as a line read from a file may: a line is its
 * length, its text NUL-terminated only for the code that reads it as a
 * string.
 */
line_t *
buf_insert_after_n(buffer_t *b, line_t *pos, const char *text, size_t len)
{
    static unsigned long next_line_id = 0;
    line_t *l = calloc(1, sizeof(line_t));
    int inserted_at_end = (b->head == NULL || pos == b->tail);
    int idx;

    if (!l) {
        return NULL;
    }
    l->text = malloc(len + 1);
    if (!l->text) {
        free(l);
        return NULL;
    }
    memcpy(l->text, text, len);
    l->text[len] = '\0';
    l->len = len;
    l->id = ++next_line_id;
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
        pos = buf_insert_after_n(dst, pos, curr->text, curr->len);
        if (pos) {
            /* the copy is the same line, for exvi_compact_undo() */
            pos->id = curr->id;
        }
        curr = curr->next;
    }
    dst->modified = src->modified;
    dst->trailing_newline = src->trailing_newline;
    exvi_history_suspended--;
}

/**
 * Make the commands run until exvi_end_undo_group() one change for undo,
 * as POSIX has a global command and the commands it runs: the buffer as it
 * is now is what undo gives back, and their own save_undo() calls keep
 * nothing.  Groups nest; the outermost decides.
 */
void
exvi_begin_undo_group(buffer_t *current)
{
    if (undo_group_depth == 0) {
        save_undo(current);
    }
    undo_group_depth++;
}

/** End what exvi_begin_undo_group() began; a group that changed nothing leaves nothing to undo. */
void
exvi_end_undo_group(void)
{
    if (undo_group_depth > 0 && --undo_group_depth == 0) {
        exvi_discard_pending_undo();
    }
}

void
save_undo(buffer_t *current)
{
    if (undo_group_depth > 0) {
        return;
    }
    exvi_discard_pending_undo();
    exvi_compact_undo(current);         /* the change before is done */
    buf_copy(&pending_undo_buf, current);
    pending_undo_valid = 1;
}

/**
 * After a file is loaded, at startup or by edit or next: ex makes its last
 * line current (POSIX ex), so `a` or `r !cmd` with no address go at the
 * end; it was the first.  vi starts on the first line.
 */
void
exvi_set_loaded_line(buffer_t *b)
{
    if (!visual_mode && b->tail) {
        b->cur = b->tail;
    }
}

/**
 * Read filename into b.  A file that does not exist is a new file, and b
 * stays empty; any other failure to open or read it is reported and marks
 * b read_failed, so that the partial buffer is not written back over the
 * file without `!`.
 */
/*
 * beautify (POSIX ex): the non-printable characters in text read from a
 * file go, but for <tab> and <form-feed>.  The new length.
 */
size_t
exvi_beautify(char *text, size_t len)
{
    size_t out = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c >= 0x20 ? c != 0x7f : (c == '\t' || c == '\f')) {
            text[out++] = (char)c;
        }
    }
    text[out] = '\0';
    return out;
}

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
            line[--ret] = '\0';
            last_had_newline = 1;
        } else {
            last_had_newline = 0;
        }
        if (option_beautify) {
            ret = (ssize_t)exvi_beautify(line, (size_t)ret);
        }
        /* every byte getline() read, a NUL too */
        pos = buf_insert_after_n(b, pos, line, (size_t)ret);
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
 * Can lines @p addr1 to @p addr2 of @p b be written: -1, -1 (the whole
 * buffer), an empty buffer's 1,0, or lines it has?
 */
static int
range_writable(buffer_t *b, int addr1, int addr2)
{
    if (addr1 == -1 || addr2 == -1) {
        return 1;
    }
    if (b->line_count == 0 && addr1 == 1 && addr2 == 0) {
        return 1;
    }
    return addr1 >= 1 && addr2 >= addr1 && addr2 <= b->line_count;
}

/**
 * Write lines addr1..addr2 of b to f (-1, -1: the whole buffer).
 * @return 0; -1 if the range is not valid, nothing written; -2 if a write
 *         failed, errno saying why
 */
int
write_range_to_stream(buffer_t *b, FILE *f, int addr1, int addr2)
{
    line_t *curr;

    if (!range_writable(b, addr1, addr2)) {
        return -1;
    }
    if (addr1 == -1 || addr2 == -1) {
        addr1 = 1;
        addr2 = b->line_count;
    }
    if (b->line_count == 0 && addr1 == 1 && addr2 == 0) {
        return 0;
    }

    if (b->empty_origin && b->line_count == 1 && addr1 == 1 && addr2 == 1
        && b->head == b->tail && b->head && b->head->len == 0
        && !b->trailing_newline) {
        return 0;
    }

    /* Every line is written with its newline -- an incomplete last line
     * read from the file too (POSIX ex, INPUT FILES).  Only the empty
     * file's placeholder line, above, writes as nothing. */
    curr = buf_get_line(b, addr1);
    for (int line = addr1; line <= addr2 && curr; line++) {
        /* the line's length, not its text up to a NUL in it */
        if (curr->len > 0 && fwrite(curr->text, 1, curr->len, f) != curr->len) {
            return -2;
        }
        if (fputc('\n', f) == EOF) {
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
        struct sigaction ign;
        struct sigaction old_pipe;
        int rc;
        int werr = 0;
        char *cmd;

        if (secure_mode || restricted_mode) {
            exvi_report_shell_forbidden();
            return -1;
        }
        /* a range that is not there runs nothing */
        if (!range_writable(b, addr1, addr2)) {
            fprintf(stderr, "Invalid write range\n");
            return -1;
        }
        cmd = exvi_shell_expand(b, filename + 1);
        if (!cmd) {
            return -1;
        }
        f = exvi_popen(cmd, "w");
        if (!f) {
            free(cmd);
            return -1;
        }
        /*
         * A command that stops reading must not take the editor with it:
         * with SIGPIPE ignored the write fails with EPIPE instead, and is
         * reported.  Ignored until pclose, which flushes what is left.
         */
        memset(&ign, 0, sizeof(ign));
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        sigaction(SIGPIPE, &ign, &old_pipe);
        errno = 0;
        rc = write_range_to_stream(b, f, addr1, addr2);
        if (rc == -2 || (rc == 0 && fflush(f) != 0)) {
            werr = errno ? errno : EIO;
        }
        exvi_pclose(f);
        sigaction(SIGPIPE, &old_pipe, NULL);
        if (rc == -1) {
            fprintf(stderr, "Invalid write range\n");
            free(cmd);
            return -1;
        }
        if (werr) {
            fprintf(stderr, "%s: %s\n", cmd, strerror(werr));
            free(cmd);
            return -1;
        }
        free(cmd);
        return 0;
    }

    /* By the file, not the name: `w ./f` while editing f writes the
     * current file too, and must leave the buffer unmodified. */
    if (exvi_is_current_file(b, filename)) {
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
