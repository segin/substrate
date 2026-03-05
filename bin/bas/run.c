#include "bas.h"

/* Execution Stack */
#define SZ_STACK 200
static struct {
    int pc;     /* Return address (offset in space) */
    int type;   /* Context type (GOSUB, FOR, etc.) */
    /* For loop storage */
    int var_idx;
    double limit;
    double step;
    int loop_start_pc;
} stack[SZ_STACK];
static int sp = 0;

/* Expression Stack */
#define SZ_ESTACK 50
static double estack[SZ_ESTACK];
static int esp = 0;

static void push(double v) {
    if (esp < SZ_ESTACK) estack[esp++] = v;
    else bas_error("Expression stack overflow");
}

static double pop() {
    if (esp > 0) return estack[--esp];
    bas_error("Expression stack underflow");
    return 0.0;
}

static void push_call(int pc, int type) {
    if (sp < SZ_STACK) {
        stack[sp].pc = pc;
        stack[sp].type = type;
        sp++;
    } else bas_error("Execution stack overflow");
}

static int pop_call(int *type) {
    if (sp > 0) {
        sp--;
        *type = stack[sp].type;
        return stack[sp].pc;
    }
    bas_error("Execution stack underflow");
    return -1;
}

/* Runtime Helper: Find which line contains a given offset */
static void update_cur_line(int pc) {
    int best_line = -1;
    int best_offset = -1;
    
    for (int i = 0; i < lintab_size; i++) {
        if (lintab[i].offset <= pc) {
            if (lintab[i].offset > best_offset) {
                best_offset = lintab[i].offset;
                best_line = lintab[i].lineno;
            }
        }
    }
    cur_line = best_line;
}

void execute_program() {
    int pc = 0;
    
    /* Find start of program */
    if (lintab_size > 0) {
        pc = lintab[0].offset;
    } else {
        return;
    }

    /* Reset stacks */
    sp = 0;
    esp = 0;

    int running = 1;
    while (running) {
        update_cur_line(pc);
        
        Instruction *inst = &space[pc];
        pc++; // Advance PC normally
        
        switch (inst->opcode) {
            case OP_END:
                /* End of line code. Check if fallthrough to next line? */
                /* In our compile model, OP_END terminates a line's bytecode. */
                /* We need to find the next line in sequence. */
                {
                    int next_line_idx = -1;
                    /* Find current line index in lintab */
                    for (int i = 0; i < lintab_size; i++) {
                        if (lintab[i].lineno == cur_line) {
                            if (i + 1 < lintab_size) {
                                next_line_idx = i + 1;
                            }
                            break;
                        }
                    }
                    if (next_line_idx != -1) {
                        pc = lintab[next_line_idx].offset;
                    } else {
                        running = 0; /* End of program */
                    }
                }
                break;
                
            case OP_PRINT:
                {
                    double v = pop();
                    printf(" %g ", v);
                }
                break;

            case OP_PRINT_NL:
                printf("\n");
                break;
                
            case OP_STR:
                printf("%s", inst->arg.s);
                break;
                
            case OP_CONST:
                push(inst->arg.f);
                break;
                
            case OP_ADD:
                {
                    double b = pop();
                    double a = pop();
                    push(a + b);
                }
                break;
                
            case OP_SUB:
                {
                    double b = pop();
                    double a = pop();
                    push(a - b);
                }
                break;
                
            case OP_MUL:
                {
                    double b = pop();
                    double a = pop();
                    push(a * b);
                }
                break;
                
            case OP_DIV:
                {
                    double b = pop();
                    double a = pop();
                    if (b == 0.0) {
                        bas_error("Division by zero");
                        push(0.0);
                    } else {
                        push(a / b);
                    }
                }
                break;

            case OP_VAR_VAL:
                push(symtab[inst->arg.i].value);
                break;
                
            case OP_LET:
                {
                    double v = pop();
                    symtab[inst->arg.i].value = v;
                    symtab[inst->arg.i].defined = 1;
                }
                break;
                
            case OP_IF:
                {
                    double cond = pop();
                    if (cond != 0.0) {
                        int target = inst->arg.target;
                        int idx = find_line(target);
                        if (idx >= 0) {
                            pc = lintab[idx].offset;
                        } else {
                            bas_error("IF GOTO undefined line");
                            running = 0;
                        }
                    }
                }
                break;

            case OP_FOR:
                {
                    double step = pop();
                    double limit = pop();
                    double init = pop();
                    int var_idx = inst->arg.i;
                    
                    symtab[var_idx].value = init;
                    symtab[var_idx].defined = 1;
                    
                    if (sp < SZ_STACK) {
                        stack[sp].type = OP_FOR; /* Reuse opcode as type */
                        stack[sp].pc = pc; /* pc is already at next instruction */
                        stack[sp].var_idx = var_idx;
                        stack[sp].limit = limit;
                        stack[sp].step = step;
                        sp++;
                    } else bas_error("Stack overflow");
                }
                break;
                
            case OP_NEXT:
                {
                    /* Find matching FOR on stack */
                    int var_idx = inst->arg.i;
                    if (sp > 0 && stack[sp-1].type == OP_FOR && stack[sp-1].var_idx == var_idx) {
                        /* Increment */
                        double val = symtab[var_idx].value;
                        double step = stack[sp-1].step;
                        double limit = stack[sp-1].limit;
                        val += step;
                        symtab[var_idx].value = val;
                        
                        /* Check limit */
                        int loop = 0;
                        if (step > 0) {
                            if (val <= limit) loop = 1;
                        } else {
                            if (val >= limit) loop = 1;
                        }
                        
                        if (loop) {
                            pc = stack[sp-1].pc;
                        } else {
                            sp--; /* Pop context */
                        }
                    } else {
                        bas_error("NEXT without FOR");
                        running = 0;
                    }
                }
                break;

            case OP_GOSUB:
                {
                    int target = inst->arg.target;
                    int idx = find_line(target);
                    if (idx >= 0) {
                        push_call(pc, OP_GOSUB);
                        pc = lintab[idx].offset;
                    } else {
                        bas_error("GOSUB undefined line");
                        running = 0;
                    }
                }
                break;
                
            case OP_RETURN:
                {
                    int type = 0;
                    int ret_pc = pop_call(&type);
                    if (type == OP_GOSUB) {
                        pc = ret_pc;
                    } else {
                        bas_error("RETURN without GOSUB");
                        running = 0;
                    }
                }
                break;
                
            case OP_BUILTIN:
                {
                    double v = pop();
                    double res = 0.0;
                    switch (inst->arg.i) {
                        case 0: res = (int)v; break; /* INT */
                        case 1: res = sqrt(v); break; /* SQR */
                        case 2: res = sin(v); break; /* SIN */
                        case 3: res = cos(v); break; /* COS */
                        case 4: res = fabs(v); break; /* ABS */
                        case 5: res = (double)rand() / RAND_MAX; break; /* RND - ignores arg */
                        case 6: res = log(v); break; /* LOG */
                        case 7: res = exp(v); break; /* EXP */
                        case 8: res = atan(v); break; /* ATN */
                    }
                    push(res);
                }
                break;
                
            case OP_GOTO:
                {
                    int target = inst->arg.target;
                    int idx = find_line(target);
                    if (idx >= 0) {
                        pc = lintab[idx].offset;
                    } else {
                        bas_error("GOTO undefined line");
                        running = 0;
                    }
                }
                break;
                
            default:
                printf("Unknown opcode %d\n", inst->opcode);
                running = 0;
                break;
        }
    }
    printf("\nDone\n");
}

void execute_immediate(const char *text) {
    (void)text;
    /* Compile immediate text to temporary space end? 
       Or separate buffer? 
       For now, stub.
    */
    printf("Immediate mode not fully implemented\n");
}
