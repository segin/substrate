#include "shell_var.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

extern long strtol(const char *nptr, char **endptr, int base);

typedef struct shell_var {
    char *name;
    char *value;
    int exported;
    struct shell_var *next;
} shell_var_t;

typedef struct shell_scope {
    shell_var_t *vars;
    struct shell_scope *next;
} shell_scope_t;

static shell_scope_t *scope_stack = NULL;

static void ensure_global_scope(void) {
    if (!scope_stack) {
        scope_stack = calloc(1, sizeof(shell_scope_t));
    }
}

// Positional parameters
static int shell_argc = 0;
static char **shell_argv = NULL;
static char *shell_name = NULL;

void shell_var_set_args(int argc, char **argv) {
    if (shell_name) free(shell_name);
    if (argc > 0) shell_name = strdup(argv[0]);
    else shell_name = strdup("sh");

    if (shell_argv) {
        for (int i = 0; i < shell_argc; i++) free(shell_argv[i]);
        free(shell_argv);
    }
    
    if (argc > 1) {
        shell_argc = argc - 1;
        shell_argv = malloc(sizeof(char *) * (shell_argc + 1));
        for (int i = 0; i < shell_argc; i++) {
            shell_argv[i] = strdup(argv[i + 1]);
        }
        shell_argv[shell_argc] = NULL;
    } else {
        shell_argc = 0;
        shell_argv = NULL;
    }
}

void shell_var_shift(int n) {
    if (n <= 0 || shell_argc == 0) return;
    if (n > shell_argc) n = shell_argc; 

    int new_argc = shell_argc - n;
    char **new_argv = NULL;
    
    if (new_argc > 0) {
        new_argv = malloc(sizeof(char *) * (new_argc + 1));
        for (int i = 0; i < new_argc; i++) {
            new_argv[i] = shell_argv[i + n];
            shell_argv[i + n] = NULL; 
        }
        new_argv[new_argc] = NULL;
    }
    
    for (int i = 0; i < n; i++) free(shell_argv[i]);
    free(shell_argv);
    
    shell_argv = new_argv;
    shell_argc = new_argc;
}

int shell_var_get_argc(void) { return shell_argc; }
char *shell_var_get_arg(int index) {
    if (index >= 0 && index < shell_argc) return shell_argv[index];
    return NULL;
}

char **shell_var_get_positional(int *count) {
    if (count) *count = shell_argc;
    return shell_argv;
}

const char *shell_var_get_name(void) { return shell_name; }

void shell_var_init(char **envp) {
    ensure_global_scope();
    if (!envp) return;
    for (int i = 0; envp[i]; i++) {
        char *eq = strchr(envp[i], '=');
        if (eq) {
            char *name = sh_strndup(envp[i], eq - envp[i]);
            shell_var_export(name, eq + 1);
            free(name);
        }
    }
}

void shell_var_print(void) {
    shell_scope_t *s = scope_stack;
    while (s) {
        shell_var_t *v = s->vars;
        while (v) {
            if (v->value) {
                printf("%s='%s'\n", v->name, v->value);
            } else {
                printf("%s=\n", v->name);
            }
            v = v->next;
        }
        s = s->next;
    }
}

char *shell_var_get(const char *name) {
    // $# - number of positional parameters
    if (strcmp(name, "#") == 0) {
        char buf[32]; snprintf(buf, sizeof(buf), "%d", shell_argc);
        return strdup(buf);
    }
    
    // $$ - process ID of the shell
    if (strcmp(name, "$") == 0) {
        char buf[32]; snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return strdup(buf);
    }
    
    // $! - PID of last background process (stored as regular variable)
    // $? - exit status (stored as regular variable)
    // These fall through to normal variable lookup below
    
    // $- - current shell options
    if (strcmp(name, "-") == 0) {
        // Return empty for now - we'd need to track set options
        return strdup("");
    }
    
    // $* - all positional parameters as single word (IFS-joined)
    if (strcmp(name, "*") == 0) {
        if (shell_argc == 0) return strdup("");
        size_t total = 0;
        for (int i = 0; i < shell_argc; i++) {
            total += strlen(shell_argv[i]) + 1;
        }
        char *buf = malloc(total + 1);
        buf[0] = 0;
        for (int i = 0; i < shell_argc; i++) {
            if (i > 0) strcat(buf, " ");
            strcat(buf, shell_argv[i]);
        }
        return buf;
    }
    
    // $@ - all positional parameters (same as $* outside double quotes)
    // Proper $@ behavior in double quotes is handled by expand.c
    if (strcmp(name, "@") == 0) {
        if (shell_argc == 0) return strdup("");
        size_t total = 0;
        for (int i = 0; i < shell_argc; i++) {
            total += strlen(shell_argv[i]) + 1;
        }
        char *buf = malloc(total + 1);
        buf[0] = 0;
        for (int i = 0; i < shell_argc; i++) {
            if (i > 0) strcat(buf, " ");
            strcat(buf, shell_argv[i]);
        }
        return buf;
    }
    
    // Positional parameters: $0, $1, ...
    char *end;
    long n = strtol(name, &end, 10);
    if (*name && !*end) {
        if (n == 0) return shell_name ? strdup(shell_name) : NULL;
        if (n > 0 && n <= shell_argc) return strdup(shell_argv[n - 1]);
        return NULL;  // Out of range
    }

    shell_scope_t *s = scope_stack;
    while (s) {
        shell_var_t *v = s->vars;
        while (v) {
            if (strcmp(v->name, name) == 0) return strdup(v->value);
            v = v->next;
        }
        s = s->next;
    }
    return NULL;
}

static shell_var_t *find_var_recursive(const char *name, shell_scope_t **found_in) {
    shell_scope_t *s = scope_stack;
    while (s) {
        shell_var_t *v = s->vars;
        while (v) {
            if (strcmp(v->name, name) == 0) {
                if (found_in) *found_in = s;
                return v;
            }
            v = v->next;
        }
        s = s->next;
    }
    return NULL;
}

void shell_var_set(const char *name, const char *value) {
    ensure_global_scope();
    shell_scope_t *s;
    shell_var_t *v = find_var_recursive(name, &s);
    if (v) {
        free(v->value);
        v->value = strdup(value);
    } else {
        // If not found, set in global scope (the last one in list)
        s = scope_stack;
        while (s->next) s = s->next;
        
        v = malloc(sizeof(shell_var_t));
        v->name = strdup(name);
        v->value = strdup(value);
        v->exported = 0;
        v->next = s->vars;
        s->vars = v;
    }
}

void shell_var_set_local(const char *name, const char *value) {
    ensure_global_scope();
    shell_scope_t *s = scope_stack; // top scope
    shell_var_t *v = s->vars;
    while (v) {
        if (strcmp(v->name, name) == 0) {
            free(v->value);
            v->value = strdup(value);
            return;
        }
        v = v->next;
    }
    v = malloc(sizeof(shell_var_t));
    v->name = strdup(name);
    v->value = strdup(value);
    v->exported = 0;
    v->next = s->vars;
    s->vars = v;
}

void shell_var_export(const char *name, const char *value) {
    shell_var_set(name, value);
    shell_var_t *v = find_var_recursive(name, NULL);
    if (v) v->exported = 1;
}

char **shell_var_get_envp(void) {
    ensure_global_scope();
    int count = 0;
    // We only collect exported vars. If shadowed, we should probably take the most recent?
    // POSIX says: variables in the environment are passed to kids.
    // If a local shadows a global exported var, which one goes to kid?
    // Usually the one that exists at the time of exec.
    
    // We'll collect unique exported names, starting from top of stack.
    typedef struct name_list { char *name; struct name_list *next; } name_list_t;
    name_list_t *seen = NULL;

    shell_scope_t *s = scope_stack;
    while (s) {
        shell_var_t *v = s->vars;
        while (v) {
            if (v->exported) {
                int found = 0;
                name_list_t *curr = seen;
                while (curr) { if (strcmp(curr->name, v->name) == 0) { found = 1; break; } curr = curr->next; }
                if (!found) {
                    count++;
                    name_list_t *n = malloc(sizeof(name_list_t));
                    n->name = v->name;
                    n->next = seen;
                    seen = n;
                }
            }
            v = v->next;
        }
        s = s->next;
    }

    char **envp = calloc(count + 1, sizeof(char *));
    int i = 0;
    name_list_t *curr = seen;
    while (curr) {
        char *val = shell_var_get(curr->name);
        size_t len = strlen(curr->name) + strlen(val) + 2;
        envp[i] = malloc(len);
        sprintf(envp[i], "%s=%s", curr->name, val);
        free(val);
        i++;
        name_list_t *tmp = curr;
        curr = curr->next;
        free(tmp);
    }
    envp[count] = NULL;
    return envp;
}

void shell_var_unset(const char *name) {
    shell_scope_t *s = scope_stack;
    while (s) {
        shell_var_t *v = s->vars;
        shell_var_t *prev = NULL;
        while (v) {
            if (strcmp(v->name, name) == 0) {
                if (prev) prev->next = v->next;
                else s->vars = v->next;
                free(v->name);
                free(v->value);
                free(v);
                return; // Unset only the first one found? Usually unset unsets *all*?
                // bash: unset unsets the variable at the current scope? No, it unsets globally?
                // POSIX: "unset name" removes the variable from the execution environment.
            }
            prev = v;
            v = v->next;
        }
        s = s->next;
    }
}

void shell_var_push_scope(void) {
    shell_scope_t *s = calloc(1, sizeof(shell_scope_t));
    s->next = scope_stack;
    scope_stack = s;
}

void shell_var_pop_scope(void) {
    if (!scope_stack || !scope_stack->next) return; // Cannot pop global
    shell_scope_t *s = scope_stack;
    scope_stack = s->next;
    shell_var_t *v = s->vars;
    while (v) {
        shell_var_t *next = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = next;
    }
    free(s);
}

typedef struct arg_stack_frame {
    int argc;
    char **argv;
    char *name;
    struct arg_stack_frame *next;
} arg_stack_frame_t;

static arg_stack_frame_t *arg_stack = NULL;

void shell_var_push_args(void) {
    arg_stack_frame_t *frame = malloc(sizeof(arg_stack_frame_t));
    frame->argc = shell_argc;
    frame->argv = shell_argv;
    frame->name = shell_name; 
    frame->next = arg_stack;
    arg_stack = frame;
    shell_argc = 0;
    shell_argv = NULL;
    shell_name = NULL;
}

void shell_var_pop_args(void) {
    if (!arg_stack) return;
    if (shell_name) free(shell_name);
    if (shell_argv) {
        for (int i=0; i<shell_argc; i++) free(shell_argv[i]);
        free(shell_argv);
    }
    arg_stack_frame_t *frame = arg_stack;
    shell_argc = frame->argc;
    shell_argv = frame->argv;
    shell_name = frame->name;
    arg_stack = frame->next;
    free(frame);
}
