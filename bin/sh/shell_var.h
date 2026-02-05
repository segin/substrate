#ifndef SHELL_VAR_H
#define SHELL_VAR_H

void shell_var_init(char **envp);
void shell_var_destroy(void);
char *shell_var_get(const char *name);
void shell_var_set(const char *name, const char *value);
void shell_var_export(const char *name, const char *value);
char **shell_var_get_envp(void);
void shell_var_unset(const char *name);
void shell_var_print(void);
const char *shell_var_get_name(void);

// Positional parameters
void shell_var_set_args(int argc, char **argv);
void shell_var_shift(int n);
int shell_var_get_argc(void);
char *shell_var_get_arg(int index);
char **shell_var_get_positional(int *count);
void shell_var_push_args(void);
void shell_var_pop_args(void);

// Scoping (for Functions)
void shell_var_push_scope(void);
void shell_var_pop_scope(void);
void shell_var_set_local(const char *name, const char *value);
void shell_var_set_readonly(const char *name);
int shell_var_is_readonly(const char *name);

#endif
