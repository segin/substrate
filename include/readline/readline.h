/*
 * readline/readline.h - GNU Readline compatibility header for libedit
 */
#ifndef _READLINE_READLINE_H_
#define _READLINE_READLINE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* readline compatible function signatures */
extern char *readline(const char *prompt);
extern int   rl_bind_key(int key, void (*func)(void));
extern void  rl_set_prompt(const char *prompt);
extern void  rl_on_new_line(void);
extern void  rl_forced_update_display(void);

/* readline compatible global variables */
extern char       *rl_line_buffer;
extern int         rl_point;
extern int         rl_end;
extern const char *rl_readline_name;
extern char      **(*rl_attempted_completion_function)(const char *, int, int);

#ifdef __cplusplus
}
#endif

#endif /* _READLINE_READLINE_H_ */
