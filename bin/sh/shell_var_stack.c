
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
    
    // Free current args (the function args)
    if (shell_name) free(shell_name);
    if (shell_argv) {
        for (int i=0; i<shell_argc; i++) free(shell_argv[i]);
        free(shell_argv);
    }
    
    // Restore
    arg_stack_frame_t *frame = arg_stack;
    shell_argc = frame->argc;
    shell_argv = frame->argv;
    shell_name = frame->name;
    arg_stack = frame->next;
    free(frame);
}
