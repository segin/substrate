#ifndef _EXVI_INTERNAL_H
#define _EXVI_INTERNAL_H

#include <stddef.h>
#include <setjmp.h>
#include <signal.h>

#include <exvi.h>

#define EXVI_DEFAULT_TABSTOP 8
#define EXVI_MIN_TABSTOP 1
#define EXVI_MAX_TABSTOP 40
#define EXVI_DEFAULT_SCROLL 12
#define EXVI_MIN_SCROLL 1
#define EXVI_MAX_SCROLL 999
#define EXVI_DEFAULT_TAGS "tags"

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
    int trailing_newline;
    char *filename;
    char *recover_filename;
    int modified;
    int empty_origin;
    int started_empty;
    line_t *marks[26];
    int mark_cols[26];
} buffer_t;

typedef struct {
    buffer_t *items;
    int len;
    int cap;
} exvi_history_t;

typedef enum {
    EXVI_COMMAND_BREAK_NONE = 0,
    EXVI_COMMAND_BREAK_SEPARATOR,
    EXVI_COMMAND_BREAK_COMMENT,
} exvi_command_break_t;

enum {
    VI_KEY_UNKNOWN = 0x100,
    VI_KEY_UP,
    VI_KEY_DOWN,
    VI_KEY_RIGHT,
    VI_KEY_LEFT,
    VI_KEY_CTRL_RIGHT,
    VI_KEY_CTRL_LEFT,
    VI_KEY_CTRL_DELETE,
    VI_KEY_CTRL_BACKSPACE,
    VI_KEY_HOME,
    VI_KEY_END,
    VI_KEY_PGUP,
    VI_KEY_PGDN,
    VI_KEY_DELETE,
    VI_KEY_RESIZE,
};

extern int secure_mode;
extern int restricted_mode;
extern int batch_mode;
extern int visual_mode;
extern int recover_mode;
extern int option_number;
extern int option_list;
extern int option_ignorecase;
extern int option_readonly;
extern int option_tabstop;
extern int option_autoindent;
extern int option_showmode;
extern int option_scroll;
extern int option_scroll_explicit;
extern int option_wrapscan;
extern char *option_tags;
extern exvi_history_t undo_history;
extern exvi_history_t redo_history;
extern buffer_t pending_undo_buf;
extern int pending_undo_valid;
extern int exvi_history_suspended;
extern char *last_search_pattern;
extern char *last_sub_pattern;
extern char *last_sub_replacement;
extern char *alternate_filename;
extern int last_sub_global;
extern const char *exvi_progname;
extern exvi_frontend_t exvi_frontend;
extern buffer_t regs[27];
extern int reg_linewise[27];
extern jmp_buf main_loop_jmp;
extern buffer_t *global_buf_for_sighandler;
extern int input_mode;
extern line_t *input_insert_pos;
extern char exvi_pending_status[256];
extern int exvi_pending_status_once;
extern int exvi_exit_requested;

void buf_init(buffer_t *b);
line_t *buf_insert_after(buffer_t *b, line_t *pos, const char *text);
void buf_delete(buffer_t *b, line_t *l);
void buf_free(buffer_t *b);
void buf_copy(buffer_t *dst, buffer_t *src);
void save_undo(buffer_t *current);
void exvi_note_buffer_change(void);
void exvi_discard_pending_undo(void);
void exvi_reset_undo_state(void);
int exvi_history_push_snapshot(exvi_history_t *history, buffer_t *src);
int exvi_history_pop_snapshot(exvi_history_t *history, buffer_t *out);
void buf_read_file(buffer_t *b, const char *filename);
void buf_write_file(buffer_t *b, const char *filename, int append);
void buf_write_range(buffer_t *b, const char *filename, int append, int addr1, int addr2);
line_t *buf_get_line(buffer_t *b, int line_num);
int buf_current_line(buffer_t *b);
int exvi_regex_flags(void);
char *parse_delimited_text(char **cmd_ptr, char delim);
int exvi_search(buffer_t *b, const char *pattern, int forward);
int exvi_decode_terminal_key_sequence(char prefix, const char *seq);
int parse_address(buffer_t *b, char **cmd_ptr);
int parse_address_checked(buffer_t *b, char **cmd_ptr, int *errorp);
int parse_range(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2);
int parse_range_checked(buffer_t *b, char **cmd_ptr, int *addr1, int *addr2,
    int *errorp);
char *find_command_break(buffer_t *b, char *cmd, exvi_command_break_t *kind);
void replace_saved_string(char **dst, const char *src);
void set_default_current_range(buffer_t *b, int *addr1, int *addr2);
void print_range(buffer_t *b, int addr1, int addr2, int numbered, int listed);
char *expand_filename_refs(buffer_t *b, const char *arg);
char *recover_path_for(const char *filename);
int load_recover_into_buffer(buffer_t *b, const char *filename);
int exvi_write_recover_snapshot(buffer_t *b, const char *path);
void exvi_cleanup_recover_file(const char *filename);
void exvi_note_recover_file(buffer_t *b, const char *filename);
void exvi_retarget_recover_file(buffer_t *b, const char *filename);
void exvi_cleanup_buffer_recover_file(buffer_t *b);
int exvi_add_startup_command(const char *cmd);
void load_startup_commands(buffer_t *b, void (*command_fn)(buffer_t *, char *));
void set_visual_handoff_file(const char *filename);
void exvi_reset_runtime(exvi_frontend_t frontend);
void exvi_cleanup_runtime(void);
void exvi_init_registers(void);
void exvi_free_registers(void);
void exvi_set_pending_status(const char *msg);
int exvi_take_pending_status(char *buf, size_t buf_size);
void exvi_report_error(const char *msg);
void exvi_report_errorf(const char *fmt, ...);
void exvi_report_shell_forbidden(void);
int exvi_restricted_filename_change(buffer_t *b, const char *target);
void exvi_cleanup_session_state(void);
void exvi_set_cli_arglist(int argc, char **argv, int optind);
void exvi_set_owned_arglist(char **args, int argc);
int exvi_has_arglist(void);
const char *exvi_current_arg(void);
void exvi_execute_command(buffer_t *b, char *cmd);
int exvi_visual_main(buffer_t *b);
void exvi_visual_shell_suspend(void);
void exvi_visual_shell_resume(void);
void handle_sigint(int sig);
void handle_sigterm(int sig);
int handle_pop_command(buffer_t *b, int force);
int handle_tag_command(buffer_t *b, const char *args, void (*command_fn)(buffer_t *, char *));
int handle_tags_command(buffer_t *b);
int handle_set_command(const char *args);
int handle_args_command(const char *args);
int handle_next_command(buffer_t *b, const char *args, int force);
int handle_prev_command(buffer_t *b, int force);
int handle_rewind_command(buffer_t *b, int force);
int handle_preserve_command(buffer_t *b);
int handle_recover_command(buffer_t *b, const char *args);
int exvi_write_allowed(buffer_t *b, const char *filename, int force);
int handle_write_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2, int force);
int handle_edit_command(buffer_t *b, const char *args, int force);
int handle_read_command(buffer_t *b, const char *args, int addr2);
int handle_delete_command(buffer_t *b, int explicit_range, int addr1, int addr2);
int default_read_destination(buffer_t *b, int addr2);
int handle_undo_command(buffer_t *b);
int handle_redo_command(buffer_t *b);
int handle_put_command(buffer_t *b, const char *args, int addr2);
int handle_print_command(buffer_t *b, const char *cmd, int explicit_range,
    int addr1, int addr2);
int handle_equal_command(buffer_t *b, int explicit_range, int addr2);
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

size_t vi_utf8_prev_offset(const char *buf, size_t pos);
int handle_substitute_command(buffer_t *b, const char *args, int addr1, int addr2);
int handle_repeat_substitute_command(buffer_t *b, const char *args, int addr1,
    int addr2);
int handle_shell_command(char *cmd);

#endif
