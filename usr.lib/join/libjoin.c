#include <libjoin.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct {
    char *line;
    size_t line_len;
    char **fields;
    int field_count;
    char *key;
} join_record_t;

typedef struct {
    FILE *f;
    const char *name;
    const join_options_t *opts;
    join_record_t *buf;
    int buf_len;
    int buf_cap;
    bool eof;
} join_reader_t;

static void free_record(join_record_t *rec) {
    if (rec->line) free(rec->line);
    if (rec->fields) free(rec->fields);
    memset(rec, 0, sizeof(*rec));
}

static void split_fields(join_record_t *rec, const join_options_t *o) {
    if (o->empty_delim) {
        rec->fields = malloc(sizeof(char *));
        rec->fields[0] = rec->line;
        rec->field_count = 1;
        return;
    }

    int cap = 4;
    rec->fields = malloc(cap * sizeof(char *));
    rec->field_count = 0;

    char *ptr = rec->line;
    if (o->delim == '\0') {
        /* Blank collapsing */
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        while (*ptr) {
            if (rec->field_count >= cap) {
                cap *= 2;
                rec->fields = realloc(rec->fields, cap * sizeof(char *));
            }
            rec->fields[rec->field_count++] = ptr;
            while (*ptr && *ptr != ' ' && *ptr != '\t') ptr++;
            if (*ptr) {
                *ptr++ = '\0';
                while (*ptr == ' ' || *ptr == '\t') ptr++;
            }
        }
    } else {
        /* Single character delimiter */
        while (1) {
            if (rec->field_count >= cap) {
                cap *= 2;
                rec->fields = realloc(rec->fields, cap * sizeof(char *));
            }
            rec->fields[rec->field_count++] = ptr;
            char *next = strchr(ptr, o->delim);
            if (next) {
                *next = '\0';
                ptr = next + 1;
            } else {
                break;
            }
        }
    }
}

static bool read_record(join_reader_t *r, join_record_t *rec, int join_field) {
    free_record(rec);
    if (r->eof) return false;

    size_t cap = 0;
    ssize_t len;
    int delim = r->opts->zero_terminated ? '\0' : '\n';
    
    len = getdelim(&rec->line, &cap, delim, r->f);
    if (len <= 0) {
        r->eof = true;
        free_record(rec);
        return false;
    }

    if (len > 0 && rec->line[len - 1] == delim) {
        rec->line[len - 1] = '\0';
        rec->line_len = len - 1;
    } else {
        rec->line_len = len;
    }

    split_fields(rec, r->opts);

    if (join_field > 0 && join_field <= rec->field_count) {
        rec->key = rec->fields[join_field - 1];
    } else {
        rec->key = "";
    }

    return true;
}

static void fill_buffer(join_reader_t *r, int join_field) {
    if (r->buf_len > 0) return; /* Already have a key buffered */

    join_record_t rec;
    memset(&rec, 0, sizeof(rec));

    if (!read_record(r, &rec, join_field)) return;

    if (r->buf_cap == 0) {
        r->buf_cap = 4;
        r->buf = malloc(r->buf_cap * sizeof(join_record_t));
    }

    r->buf[0] = rec;
    r->buf_len = 1;

    /* Read all identical keys into buffer */
    while (1) {
        join_record_t next_rec;
        memset(&next_rec, 0, sizeof(next_rec));

        /* We must peek ahead. getdelim advances the file ptr, so we buffer it if it matches. */
        off_t pos = ftello(r->f);
        if (!read_record(r, &next_rec, join_field)) break;

        int cmp;
        if (r->opts->ignore_case) {
            cmp = strcasecmp(r->buf[0].key, next_rec.key);
        } else {
            cmp = strcmp(r->buf[0].key, next_rec.key);
        }

        if (cmp == 0) {
            if (r->buf_len >= r->buf_cap) {
                r->buf_cap *= 2;
                r->buf = realloc(r->buf, r->buf_cap * sizeof(join_record_t));
            }
            r->buf[r->buf_len++] = next_rec;
        } else {
            /* Not a match, rewind and free peeked record */
            fseeko(r->f, pos, SEEK_SET);
            free_record(&next_rec);
            r->eof = false; /* We might have hit EOF on the peek, restore state */
            break;
        }
    }
}

static void consume_buffer(join_reader_t *r) {
    for (int i = 0; i < r->buf_len; i++) {
        free_record(&r->buf[i]);
    }
    r->buf_len = 0;
}

static void print_field(FILE *out, const char *field, const join_options_t *opts, bool first) {
    if (!first) {
        char out_delim = (opts->delim == '\0') ? ' ' : opts->delim;
        fputc(out_delim, out);
    }
    if (!field || *field == '\0') {
        if (opts->empty_str) fputs(opts->empty_str, out);
    } else {
        fputs(field, out);
    }
}

static void print_unpaired(FILE *out, const join_record_t *rec, int file_idx, const join_options_t *opts) {
    bool first = true;
    print_field(out, rec->key, opts, first);
    first = false;

    if (opts->out_list) {
        for (int i = 0; i < opts->out_list_len; i++) {
            int f = opts->out_list[i].file_idx;
            int idx = opts->out_list[i].field_idx;
            
            if (f == 0) {
                print_field(out, rec->key, opts, first);
            } else if (f == file_idx) {
                const char *val = (idx <= rec->field_count) ? rec->fields[idx - 1] : "";
                print_field(out, val, opts, first);
            } else {
                print_field(out, "", opts, first);
            }
        }
    } else {
        int jf = (file_idx == 1) ? opts->join_field_1 : opts->join_field_2;
        for (int i = 0; i < rec->field_count; i++) {
            if (i + 1 != jf) print_field(out, rec->fields[i], opts, first);
        }
    }

    fputc(opts->zero_terminated ? '\0' : '\n', out);
}

static void print_paired(FILE *out, const join_record_t *r1, const join_record_t *r2, const join_options_t *opts) {
    bool first = true;
    
    if (opts->out_list) {
        for (int i = 0; i < opts->out_list_len; i++) {
            int f = opts->out_list[i].file_idx;
            int idx = opts->out_list[i].field_idx;
            
            if (f == 0) {
                print_field(out, r1->key, opts, first);
            } else if (f == 1) {
                const char *val = (idx <= r1->field_count) ? r1->fields[idx - 1] : "";
                print_field(out, val, opts, first);
            } else if (f == 2) {
                const char *val = (idx <= r2->field_count) ? r2->fields[idx - 1] : "";
                print_field(out, val, opts, first);
            }
            first = false;
        }
    } else {
        print_field(out, r1->key, opts, first);
        first = false;
        
        for (int i = 0; i < r1->field_count; i++) {
            if (i + 1 != opts->join_field_1) print_field(out, r1->fields[i], opts, first);
        }
        for (int i = 0; i < r2->field_count; i++) {
            if (i + 1 != opts->join_field_2) print_field(out, r2->fields[i], opts, first);
        }
    }
    
    fputc(opts->zero_terminated ? '\0' : '\n', out);
}

int join_files(FILE *f1, const char *name1, FILE *f2, const char *name2, const join_options_t *opts, FILE *out) {
    join_reader_t r1 = {f1, name1, opts, NULL, 0, 0, false};
    join_reader_t r2 = {f2, name2, opts, NULL, 0, 0, false};

    while (!r1.eof || !r2.eof || r1.buf_len > 0 || r2.buf_len > 0) {
        fill_buffer(&r1, opts->join_field_1);
        fill_buffer(&r2, opts->join_field_2);

        if (r1.buf_len > 0 && r2.buf_len > 0) {
            int cmp;
            if (opts->ignore_case) {
                cmp = strcasecmp(r1.buf[0].key, r2.buf[0].key);
            } else {
                cmp = strcmp(r1.buf[0].key, r2.buf[0].key);
            }

            if (cmp == 0) {
                /* Match: Cartesian product */
                if (!opts->unpair_only_1 && !opts->unpair_only_2) {
                    for (int i = 0; i < r1.buf_len; i++) {
                        for (int j = 0; j < r2.buf_len; j++) {
                            print_paired(out, &r1.buf[i], &r2.buf[j], opts);
                        }
                    }
                }
                consume_buffer(&r1);
                consume_buffer(&r2);
            } else if (cmp < 0) {
                /* r1 key is smaller */
                if (opts->unpair_1 || opts->unpair_only_1) {
                    for (int i = 0; i < r1.buf_len; i++) print_unpaired(out, &r1.buf[i], 1, opts);
                }
                consume_buffer(&r1);
            } else {
                /* r2 key is smaller */
                if (opts->unpair_2 || opts->unpair_only_2) {
                    for (int i = 0; i < r2.buf_len; i++) print_unpaired(out, &r2.buf[i], 2, opts);
                }
                consume_buffer(&r2);
            }
        } else if (r1.buf_len > 0) {
            if (opts->unpair_1 || opts->unpair_only_1) {
                for (int i = 0; i < r1.buf_len; i++) print_unpaired(out, &r1.buf[i], 1, opts);
            }
            consume_buffer(&r1);
        } else if (r2.buf_len > 0) {
            if (opts->unpair_2 || opts->unpair_only_2) {
                for (int i = 0; i < r2.buf_len; i++) print_unpaired(out, &r2.buf[i], 2, opts);
            }
            consume_buffer(&r2);
        }
    }

    if (r1.buf) free(r1.buf);
    if (r2.buf) free(r2.buf);

    return 0;
}
