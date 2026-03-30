#ifndef _EXVI_INTERNAL_H
#define _EXVI_INTERNAL_H

#include <stddef.h>
#include <setjmp.h>
#include <signal.h>

#include <exvi.h>

#define EXVI_DEFAULT_TABSTOP 8
#define EXVI_MIN_TABSTOP 1
#define EXVI_MAX_TABSTOP 40

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
    int mark_cols[26];
} buffer_t;

extern int secure_mode;
extern int batch_mode;
extern int visual_mode;
extern int recover_mode;
extern int option_number;
extern int option_list;
extern int option_tabstop;
extern buffer_t undo_buf;
extern int undo_valid;
extern char *last_search_pattern;
extern char *last_sub_pattern;
extern char *last_sub_replacement;
extern char *alternate_filename;
extern int last_sub_global;
extern const char *exvi_progname;
extern buffer_t regs[27];
extern int reg_linewise[27];
extern jmp_buf main_loop_jmp;
extern buffer_t *global_buf_for_sighandler;
extern int input_mode;
extern line_t *input_insert_pos;

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
int exvi_search(buffer_t *b, const char *pattern, int forward);
int parse_address(buffer_t *b, char **cmd_ptr);
int parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2);
void replace_saved_string(char **dst, const char *src);
void set_default_current_range(buffer_t *b, int *addr1, int *addr2);
void print_range(buffer_t *b, int addr1, int addr2, int numbered, int listed);
char *expand_filename_refs(buffer_t *b, const char *arg);
char *recover_path_for(const char *filename);
int load_recover_into_buffer(buffer_t *b, const char *filename);
void load_startup_commands(buffer_t *b, void (*command_fn)(buffer_t *, char *));
void set_visual_handoff_file(const char *filename);
void exvi_reset_runtime(exvi_frontend_t frontend);
void exvi_cleanup_runtime(void);
void exvi_init_registers(void);
void exvi_free_registers(void);
void exvi_cleanup_session_state(void);
void exvi_set_cli_arglist(int argc, char **argv, int optind);
int exvi_has_arglist(void);
const char *exvi_current_arg(void);
void exvi_execute_command(buffer_t *b, char *cmd);
int exvi_visual_main(buffer_t *b);
void handle_sigint(int sig);
void handle_sigterm(int sig);
int handle_pop_command(buffer_t *b, int force);
int handle_tag_command(buffer_t *b, const char *args, void (*command_fn)(buffer_t *, char *));
int handle_set_command(const char *args);
int handle_args_command(const char *args);
int handle_next_command(buffer_t *b, const char *args, int force);
int handle_prev_command(buffer_t *b, int force);
int handle_rewind_command(buffer_t *b, int force);
int handle_preserve_command(buffer_t *b);
int handle_recover_command(buffer_t *b, const char *args);
int handle_write_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2);
int handle_edit_command(buffer_t *b, const char *args, int force);
int handle_read_command(buffer_t *b, const char *args, int addr2);
int handle_delete_command(buffer_t *b, int explicit_range, int addr1, int addr2);
int default_read_destination(buffer_t *b, int addr2);
int handle_undo_command(buffer_t *b);
int handle_put_command(buffer_t *b, const char *args, int addr2);
int handle_print_command(buffer_t *b, const char *cmd, int explicit_range,
    int addr1, int addr2);
int handle_mark_command(buffer_t *b, const char *cmd, const char *args, int addr2);
int handle_file_command(buffer_t *b, const char *args);
int handle_input_command(buffer_t *b, int mode, int explicit_range, int addr1,
    int addr2);
int handle_copy_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2);
int handle_move_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2);
int handle_join_command(buffer_t *b, int explicit_range, int addr1, int addr2);
int handle_yank_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2);
int handle_substitute_command(buffer_t *b, const char *args, int addr1, int addr2);
int handle_repeat_substitute_command(buffer_t *b, int addr1, int addr2);
int handle_shell_command(char *cmd);

#endif
