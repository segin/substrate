#ifndef _EXVI_INTERNAL_H
#define _EXVI_INTERNAL_H

#include <stddef.h>

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

extern int secure_mode;
extern buffer_t undo_buf;
extern int undo_valid;
extern char *last_search_pattern;

void buf_init(buffer_t *b);
line_t *buf_insert_after(buffer_t *b, line_t *pos, const char *text);
void buf_delete(buffer_t *b, line_t *l);
void buf_free(buffer_t *b);
void buf_copy(buffer_t *dst, buffer_t *src);
void save_undo(buffer_t *current);
void buf_read_file(buffer_t *b, const char *filename);
void buf_write_file(buffer_t *b, const char *filename, int append);
void buf_write_range(buffer_t *b, const char *filename, int append, int addr1, int addr2);
line_t *buf_get_line(buffer_t *b, int line_num);
int buf_current_line(buffer_t *b);
char *parse_delimited_text(char **cmd_ptr, char delim);
int parse_address(buffer_t *b, char **cmd_ptr);
int parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2);
void replace_saved_string(char **dst, const char *src);

#endif
