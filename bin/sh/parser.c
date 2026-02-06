#include "parser.h"
#include "shell_var.h"
#include <string.h>
#include <stdio.h>
#include "util.h"

static ast_node_t *parse_list(lexer_t *l);
static ast_node_t *parse_command(lexer_t *l);
static ast_node_t *parse_if(lexer_t *l);
static ast_node_t *parse_while(lexer_t *l);
static ast_node_t *parse_for(lexer_t *l);
static ast_node_t *parse_for(lexer_t *l);
static ast_node_t *parse_case(lexer_t *l);
static ast_node_t *parse_subshell(lexer_t *l);
static ast_node_t *parse_group(lexer_t *l);
static ast_redirection_t *parse_redirections(lexer_t *l);
static int is_reserved(const char *val, const char *word);

static void parser_error(lexer_t *l, const char *msg) {
    token_t *t = lexer_peek(l);
    if (t) {
        fprintf(stderr, "%s: syntax error near unexpected token `%s'\n", shell_var_get_name(), t->value);
    } else {
        fprintf(stderr, "%s: syntax error: %s\n", shell_var_get_name(), msg ? msg : "unexpected EOF");
    }
}

static ast_simple_command_t *create_simple_command(void) {
    ast_simple_command_t *cmd = calloc(1, sizeof(ast_simple_command_t));
    cmd->base.type = NODE_SIMPLE_COMMAND;
    cmd->base.redirections = NULL;
    cmd->arg_capacity = 8;
    cmd->args = calloc(cmd->arg_capacity + 1, sizeof(char*));
    cmd->assign_capacity = 4;
    cmd->assignments = calloc(cmd->assign_capacity + 1, sizeof(char*));
    return cmd;
}

static void free_redirections(ast_redirection_t *r) {
    while (r) {
        ast_redirection_t *next = r->next;
        if (r->filename) free(r->filename);
        if (r->heredoc_content) free(r->heredoc_content);
        free(r);
        r = next;
    }
}

void ast_free(ast_node_t *node) {
    if (!node) return;
    
    // Free redirections common to all nodes
    free_redirections(node->redirections);

    if (node->type == NODE_SIMPLE_COMMAND) {
        ast_simple_command_t *cmd = (ast_simple_command_t*)node;
        for (int i = 0; i < cmd->arg_count; i++) {
            free(cmd->args[i]);
        }
        free(cmd->args);
        for (int i = 0; i < cmd->assign_count; i++) {
            free(cmd->assignments[i]);
        }
        free(cmd->assignments);
        free(cmd);
    } else if (node->type == NODE_PIPELINE) {
        ast_pipeline_t *pipe = (ast_pipeline_t*)node;
        for (int i = 0; i < pipe->command_count; i++) {
            ast_free(pipe->commands[i]);
        }
        free(pipe->commands);
        free(pipe);
    } else if (node->type == NODE_BINARY_OP) {
        ast_binary_op_t *bin = (ast_binary_op_t*)node;
        ast_free(bin->left);
        ast_free(bin->right);
        free(bin);
    } else if (node->type == NODE_IF) {
        ast_if_t *if_node = (ast_if_t*)node;
        ast_free(if_node->condition);
        ast_free(if_node->then_body);
        ast_free(if_node->else_body);
        free(if_node);
    } else if (node->type == NODE_WHILE) {
        ast_while_t *w = (ast_while_t*)node;
        ast_free(w->condition);
        ast_free(w->body);
        free(w);
    } else if (node->type == NODE_FOR) {
        ast_for_t *f = (ast_for_t*)node;
        if (f->var_name) free(f->var_name);
        if (f->elements) {
            for (int i=0; i<f->element_count; i++) free(f->elements[i]);
            free(f->elements);
        }
        ast_free(f->body);
        free(f);
    } else if (node->type == NODE_FUNCTION) {
        ast_function_t *func = (ast_function_t*)node;
        if (func->name) free(func->name);
        ast_free(func->body);
        free(func);
    } else if (node->type == NODE_SUBSHELL) {
        ast_subshell_t *sub = (ast_subshell_t*)node;
        ast_free(sub->list);
        free(sub);
    } else if (node->type == NODE_CASE) {
        ast_case_t *c = (ast_case_t*)node;
        if (c->word) free(c->word);
        ast_case_item_t *item = c->items;
        while (item) {
            ast_case_item_t *next = item->next;
            if (item->pattern) free(item->pattern);
            ast_free(item->body);
            free(item);
            item = next;
        }
        free(c);
    }
}

char *ast_to_string(ast_node_t *node) {
    if (!node) return strdup("");
    
    if (node->type == NODE_SIMPLE_COMMAND) {
        ast_simple_command_t *cmd = (ast_simple_command_t *)node;
        if (cmd->arg_count == 0) return strdup("(assignment)");
        size_t len = 0;
        for (int i = 0; i < cmd->arg_count; i++) len += strlen(cmd->args[i]) + 1;
        char *res = malloc(len + 1);
        res[0] = 0;
        for (int i = 0; i < cmd->arg_count; i++) {
            strcat(res, cmd->args[i]);
            if (i < cmd->arg_count - 1) strcat(res, " ");
        }
        return res;
    }
    if (node->type == NODE_PIPELINE) {
        ast_pipeline_t *pipe = (ast_pipeline_t *)node;
        char *res = strdup("");
        for (int i = 0; i < pipe->command_count; i++) {
            char *s = ast_to_string(pipe->commands[i]);
            size_t new_len = strlen(res) + strlen(s) + 4;
            char *next = malloc(new_len);
            sprintf(next, "%s%s%s", res, (i == 0 ? "" : " | "), s);
            free(res);
            free(s);
            res = next;
        }
        return res;
    }
    if (node->type == NODE_BINARY_OP) {
        ast_binary_op_t *bin = (ast_binary_op_t *)node;
        char *s1 = ast_to_string(bin->left);
        char *s2 = bin->right ? ast_to_string(bin->right) : strdup("");
        const char *op = " ; ";
        if (bin->op == OP_AND) op = " && ";
        else if (bin->op == OP_OR) op = " || ";
        else if (bin->op == OP_BACKGROUND) op = " & ";
        
        char *res = malloc(strlen(s1) + strlen(s2) + strlen(op) + 1);
        sprintf(res, "%s%s%s", s1, op, s2);
        free(s1);
        free(s2);
        return res;
    }
    return strdup("builtin/compound");
}

static void cmd_add_arg(ast_simple_command_t *cmd, char *arg) {
    if (cmd->arg_count >= cmd->arg_capacity) {
        cmd->arg_capacity *= 2;
        cmd->args = realloc(cmd->args, (cmd->arg_capacity + 1) * sizeof(char*));
    }
    cmd->args[cmd->arg_count++] = strdup(arg);
    cmd->args[cmd->arg_count] = NULL;
}

static void cmd_add_assign(ast_simple_command_t *cmd, char *arg) {
    if (cmd->assign_count >= cmd->assign_capacity) {
        cmd->assign_capacity *= 2;
        cmd->assignments = realloc(cmd->assignments, (cmd->assign_capacity + 1) * sizeof(char*));
    }
    cmd->assignments[cmd->assign_count++] = strdup(arg);
    cmd->assignments[cmd->assign_count] = NULL;
}

static void cmd_add_redir(ast_simple_command_t *cmd, int fd, redir_type_t type, char *filename) {
    ast_redirection_t *r = calloc(1, sizeof(ast_redirection_t));
    r->fd = fd;
    r->type = type;
    r->filename = filename; // Takes ownership
    
    // Append to list (inefficient but fine for shell commands)
    if (!cmd->base.redirections) {
        cmd->base.redirections = r;
    } else {
        ast_redirection_t *curr = cmd->base.redirections;
        while (curr->next) curr = curr->next;
        curr->next = r;
    }
}

static ast_redirection_t *parse_redirections(lexer_t *l) {
    ast_redirection_t *head = NULL;
    ast_redirection_t *tail = NULL;
    
    while (1) {
        token_t *t = lexer_peek(l);
        if (!t) break;
        
        int fd = -1;
        
        // Check for IO_NUMBER (e.g. "2>")
        if (t->type == TOKEN_IO_NUMBER) {
            token_t *next = lexer_peek2(l);
            if (next && next->type == TOKEN_OPERATOR && 
                (strcmp(next->value, "<") == 0 || strcmp(next->value, ">") == 0 || 
                 strcmp(next->value, ">>") == 0 || strcmp(next->value, "<<") == 0 ||
                 strcmp(next->value, "<&") == 0 || strcmp(next->value, ">&") == 0)) {
                 
                 fd = atoi(t->value);
                 token_free(lexer_next(l)); // consume number
                 t = lexer_peek(l); // should be op
            } else {
                break;
            }
        }
        
        if (!t || t->type != TOKEN_OPERATOR) break;
        
        redir_type_t type;
        if (t->value && strcmp(t->value, "<") == 0) type = REDIR_IN;
        else if (t->value && strcmp(t->value, ">") == 0) type = REDIR_OUT;
        else if (t->value && strcmp(t->value, ">>") == 0) type = REDIR_APPEND;
        else if (t->value && strcmp(t->value, "<<") == 0) type = REDIR_HERE_DOC;
        else if (t->value && strcmp(t->value, "<&") == 0) type = REDIR_DUP_IN;
        else if (t->value && strcmp(t->value, ">&") == 0) type = REDIR_DUP_OUT;
        else break;
        
        if (fd == -1) {
            if (type == REDIR_IN || type == REDIR_HERE_DOC) fd = 0;
            else fd = 1;
        }
        
        token_free(lexer_next(l)); // consume op
        
        token_t *file = lexer_next(l);
        if (!file || file->type != TOKEN_WORD) {
            if (file) token_free(file);
            fprintf(stderr, "%s: syntax error: expected filename after redirection\n", shell_var_get_name());
            break; 
        }
        
        ast_redirection_t *r = calloc(1, sizeof(ast_redirection_t));
        r->fd = fd;
        r->type = type;
        r->filename = strdup(file->value);
        r->quoted = file->quoted;
        token_free(file);
        
        if (!head) head = r;
        else tail->next = r;
        tail = r;
    }
    return head;
}

static char *read_heredoc(lexer_t *l, const char *delim) {
    // Check if we need to skip the rest of the current line.
    // parse_simple_command loops until a terminator (newline, ;, etc) is peeked.
    // If the terminator was a newline, the lexer scan ALREADY advanced l->pos past it.
    token_t *t = lexer_peek(l);
    int skip = 1;
    if (t && t->type == TOKEN_NEWLINE) skip = 0;
    
    if (skip) {
        while (l->pos < l->len && l->input[l->pos] != '\n') {
            l->pos++;
        }
        if (l->pos < l->len) l->pos++; // Consume \n
    }

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    buf[0] = 0;

    size_t line_start = l->pos;
    while (l->pos < l->len) {
        if (l->input[l->pos] == '\n') {
            size_t line_len = l->pos - line_start;
            
            // Check delimiter
            if (line_len == strlen(delim) && strncmp(l->input + line_start, delim, line_len) == 0) {
                l->pos++; // Consume \n
                lexer_clear_lookahead(l);
                return buf;
            }
            
            // Append line + \n
            while (len + line_len + 1 >= cap) cap *= 2;
            buf = realloc(buf, cap);
            memcpy(buf + len, l->input + line_start, line_len + 1);
            len += line_len + 1;
            buf[len] = 0;
            
            l->pos++;
            line_start = l->pos;
        } else {
            l->pos++;
        }
    }
    lexer_clear_lookahead(l);
    return buf;
}

static ast_node_t *parse_simple_command(lexer_t *l) {
    // For now, simplistically expect a simple command
    
    token_t *t = lexer_peek(l);
    if (!t || t->type == TOKEN_EOF || t->type == TOKEN_NEWLINE) {
        // Empty command
        return NULL;
    }

    ast_simple_command_t *cmd = create_simple_command();
    
    // pending IO number (e.g. "2" in "2>")
    int pending_fd = -1;

    while (1) {
        t = lexer_peek(l);
        if (!t || t->type == TOKEN_EOF || t->type == TOKEN_NEWLINE) {
            break;
        }

        if (t->type == TOKEN_WORD) {
            // Check if it's a reserved word (e.g. 'done', 'fi', 'then')
            // These cannot be the command name in a simple command if they start a block
            // or terminate one.
            if (cmd->arg_count == 0 && (
                is_reserved(t->value, "if") || is_reserved(t->value, "then") || 
                is_reserved(t->value, "else") || is_reserved(t->value, "elif") || 
                is_reserved(t->value, "fi") || is_reserved(t->value, "while") || 
                is_reserved(t->value, "do") || is_reserved(t->value, "done") ||
                is_reserved(t->value, "for") || is_reserved(t->value, "case") ||
                is_reserved(t->value, "esac") || is_reserved(t->value, "{") ||
                is_reserved(t->value, "}") || is_reserved(t->value, "(") ||
                is_reserved(t->value, ")"))) {
                break;
            }

            t = lexer_next(l); // Consume
            
            if (cmd->arg_count == 0 && strchr(t->value, '=')) {
                char *eq = strchr(t->value, '=');
                if (eq > t->value) {
                     cmd_add_assign(cmd, t->value);
                } else {
                    cmd_add_arg(cmd, t->value);
                }
            } else {
                cmd_add_arg(cmd, t->value);
            }
            token_free(t);
        }
        else if (t->type == TOKEN_IO_NUMBER) {
             // Peek ahead to see if next is operator
             // Actually, lexer only produces IO_NUMBER if followed by < or >.
             // So we can consume it, but we need to be sure we are consuming the *operator* too.
             // But let's stick to the logic: consume IO_NUMBER, then must consume operator.
             
             t = lexer_next(l); // Consume IO number
             int fd = atoi(t->value);
             token_free(t);
             
             pending_fd = fd;
             // Continue to next iteration to handle operator?
             // No, standard says IO_NUMBER must be immediately followed by operator.
             // So we should check/consume operator now.
             
             t = lexer_peek(l);
             if (!t || t->type != TOKEN_OPERATOR) {
                 // Error or just treat "2" as arg if we were loose?
                 // But Lexer definition of IO_NUMBER is strict.
                 // Treat as error for now or fallback?
                 // Let's assume valid grammar for this task.
             }
        }
        else if (t->type == TOKEN_OPERATOR) {
            // Check if it's a redirection operator vs control operator
            if (t->value && (strcmp(t->value, ";") == 0 || strcmp(t->value, "&") == 0 || strcmp(t->value, "|") == 0 ||
                strcmp(t->value, "&&") == 0 || strcmp(t->value, "||") == 0 || strcmp(t->value, "(") == 0 || strcmp(t->value, ")") == 0 ||
                strcmp(t->value, ";;") == 0)) {
                // Control operator - End of Simple Command
                break;
            }
            
            t = lexer_next(l); // Consume operator
            
            int fd = (pending_fd != -1) ? pending_fd : -1;
            pending_fd = -1;
             
            redir_type_t rtype;
            if (strcmp(t->value, "<") == 0) {
                rtype = REDIR_IN;
                if (fd == -1) fd = 0;
            } else if (strcmp(t->value, ">") == 0) {
                 rtype = REDIR_OUT;
                 if (fd == -1) fd = 1;
            } else if (strcmp(t->value, ">>") == 0) {
                 rtype = REDIR_APPEND;
                 if (fd == -1) fd = 1;
            } else if (strcmp(t->value, "<<") == 0) {
                 rtype = REDIR_HERE_DOC;
                 if (fd == -1) fd = 0;
            } else if (strcmp(t->value, "<&") == 0) { // Future
                 rtype = REDIR_DUP_IN;
            } else if (strcmp(t->value, ">&") == 0) { // Future
                 rtype = REDIR_DUP_OUT;
            } else {
                 // Unknown operator? Should have caught above.
                 token_free(t);
                 continue; 
            }
            token_free(t);

            // Consume filename
            token_t *file = lexer_next(l);
            if (!file || file->type != TOKEN_WORD) {
                 // Error: expected filename
                 if (file) token_free(file);
                 ast_free((ast_node_t*)cmd);
                 return NULL;
            }
            
            if (rtype == REDIR_HERE_DOC) {
                unquote_word(file->value);
            }
             
            cmd_add_redir(cmd, fd, rtype, file->value);
            // We need to pass the quoted flag to cmd_add_redir or set it manually
            // Let's set it manually on the last added redirection
            ast_redirection_t *last = cmd->base.redirections;
            while (last->next) last = last->next;
            last->quoted = file->quoted;
            free(file);
        }
        else {
            if (t && t->type == TOKEN_ERROR) {
                parser_error(l, t->value);
            } else {
                parser_error(l, "unexpected token in command");
            }
            ast_free((ast_node_t*)cmd);
            return NULL;
        }
    }

    if (cmd->arg_count == 0 && cmd->base.redirections == NULL && cmd->assign_count == 0) {
        ast_free((ast_node_t*)cmd);
        return NULL;
    }

    // Process here-docs
    if (cmd->base.redirections) {
        ast_redirection_t *r = cmd->base.redirections;
        while (r) {
            if (r->type == REDIR_HERE_DOC) {
                r->heredoc_content = read_heredoc(l, r->filename);
            }
            r = r->next;
        }
    }

    return (ast_node_t*)cmd;
}

// Forward declarations
static ast_node_t *parse_list(lexer_t *l);

static int is_reserved(const char *val, const char *word) {
    if (!val || !word) return 0;
    return strcmp(val, word) == 0;
}

static void consume_newlines(lexer_t *l) {
    while (1) {
        token_t *t = lexer_peek(l);
        if (t && t->type == TOKEN_NEWLINE) {
            token_free(lexer_next(l));
        } else {
            break;
        }
    }
}

static ast_node_t *parse_else_part(lexer_t *l) {
    token_t *t = lexer_peek(l);
    if (!t || t->type != TOKEN_WORD) return NULL;

    if (is_reserved(t->value, "else")) {
        token_free(lexer_next(l)); // consume 'else'
        return parse_list(l);
    } else if (is_reserved(t->value, "elif")) {
        token_free(lexer_next(l)); // consume 'elif'
        ast_if_t *elif_node = calloc(1, sizeof(ast_if_t));
        elif_node->base.type = NODE_IF;
        elif_node->condition = parse_list(l);
        
        t = lexer_next(l);
        if (!t || !is_reserved(t->value, "then")) {
            if (t) token_free(t);
            ast_free((ast_node_t*)elif_node);
            return NULL;
        }
        token_free(t);
        
        elif_node->then_body = parse_list(l);
        elif_node->else_body = parse_else_part(l);
        return (ast_node_t*)elif_node;
    }
    return NULL;
}

static ast_node_t *parse_if(lexer_t *l) {
    token_free(lexer_next(l)); // consume 'if'
    
    ast_if_t *node = calloc(1, sizeof(ast_if_t));
    node->base.type = NODE_IF;
    
    node->condition = parse_list(l);
    if (!node->condition) {
        ast_free((ast_node_t*)node);
        return NULL;
    }

    token_t *t = lexer_next(l);
    if (!t || !is_reserved(t->value, "then")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    
    node->then_body = parse_list(l);
    node->else_body = parse_else_part(l);

    t = lexer_next(l);
    if (!t || !is_reserved(t->value, "fi")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    
    node->base.redirections = parse_redirections(l);
    return (ast_node_t*)node;
}

static ast_node_t *parse_while(lexer_t *l) {
    token_free(lexer_next(l)); // consume 'while'
    
    ast_while_t *node = calloc(1, sizeof(ast_while_t));
    node->base.type = NODE_WHILE;
    
    node->condition = parse_list(l);
    
    token_t *t = lexer_next(l);
    if (!t || t->type != TOKEN_WORD || !is_reserved(t->value, "do")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    
    node->body = parse_list(l);
    
    consume_newlines(l);
    t = lexer_next(l);
    if (!t || t->type != TOKEN_WORD || !is_reserved(t->value, "done")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    
    node->base.redirections = parse_redirections(l);
    return (ast_node_t*)node;
}

static ast_node_t *parse_subshell(lexer_t *l) {
    token_free(lexer_next(l)); // consume '('
    ast_subshell_t *node = calloc(1, sizeof(ast_subshell_t));
    node->base.type = NODE_SUBSHELL;
    node->list = parse_list(l);
    
    token_t *t = lexer_next(l);
    if (!t || !t->value || strcmp(t->value, ")") != 0) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    return (ast_node_t*)node;
}

static ast_node_t *parse_group(lexer_t *l) {
    token_free(lexer_next(l)); // consume '{'
    ast_node_t *node = parse_list(l);
    
    token_t *t = lexer_next(l);
    if (!t || !t->value || strcmp(t->value, "}") != 0) {
        if (t) token_free(t);
        ast_free(node);
        return NULL;
    }
    token_free(t);
    
    node->redirections = parse_redirections(l); // base.redirections
    return node;
}

static ast_node_t *parse_for(lexer_t *l) {
    token_free(lexer_next(l)); // consume 'for'
    
    ast_for_t *node = calloc(1, sizeof(ast_for_t));
    node->base.type = NODE_FOR;
    
    token_t *t = lexer_next(l);
    if (!t || t->type != TOKEN_WORD) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    node->var_name = strdup(t->value);
    token_free(t);
    
    consume_newlines(l);
    
    t = lexer_peek(l);
    if (t && t->type == TOKEN_WORD && is_reserved(t->value, "in")) {
        token_free(lexer_next(l)); // consume 'in'
        while (1) {
            t = lexer_peek(l);
            if (!t) break;
            if (t->type == TOKEN_OPERATOR && strcmp(t->value, ";") == 0) {
                token_free(lexer_next(l));
                break;
            }
            if (t->type == TOKEN_NEWLINE) {
                token_free(lexer_next(l));
                break;
            }
            if (t->type == TOKEN_WORD && is_reserved(t->value, "do")) {
                break;
            }
            
            if (t->type == TOKEN_WORD) {
                t = lexer_next(l);
                if (node->element_count >= node->element_capacity) {
                    node->element_capacity = (node->element_capacity == 0) ? 8 : node->element_capacity * 2;
                    node->elements = realloc(node->elements, (node->element_capacity + 1) * sizeof(char*));
                }
                node->elements[node->element_count++] = strdup(t->value);
                node->elements[node->element_count] = NULL;
                token_free(t);
            } else {
                break; 
            }
        }
    }
    
    consume_newlines(l);
    
    t = lexer_next(l);
    if (!t || !is_reserved(t->value, "do")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    
    node->body = parse_list(l);
    
    t = lexer_next(l);
    if (!t || !is_reserved(t->value, "done")) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    token_free(t);
    return (ast_node_t*)node;
}


static ast_node_t *parse_command(lexer_t *l) {
    consume_newlines(l);
    token_t *t = lexer_peek(l);
    if (!t || t->type == TOKEN_EOF) return NULL;
    
    if (t->type == TOKEN_OPERATOR && strcmp(t->value, "(") == 0) {
        return parse_subshell(l);
    }

    if (t->type == TOKEN_WORD) {
        if (is_reserved(t->value, "if")) return parse_if(l);
        if (is_reserved(t->value, "while")) return parse_while(l);
        if (is_reserved(t->value, "for")) return parse_for(l);
        if (is_reserved(t->value, "case")) return parse_case(l);
        if (is_reserved(t->value, "{")) return parse_group(l);
        
        // Check for function definition: name ( )
        token_t *next = lexer_peek2(l);
        if (next && next->type == TOKEN_OPERATOR && strcmp(next->value, "(") == 0) {
             token_t *next2 = lexer_peek_n(l, 2);
             if (next2 && next2->type == TOKEN_OPERATOR && strcmp(next2->value, ")") == 0) {
                 token_t *t_name = lexer_next(l);
                 token_free(lexer_next(l)); // consume '('
                 token_free(lexer_next(l)); // consume ')'
                 consume_newlines(l);
                 
                 ast_function_t *fn = calloc(1, sizeof(ast_function_t));
                 fn->base.type = NODE_FUNCTION;
                 fn->name = strdup(t_name->value);
                 token_free(t_name);
                 
                 fn->body = parse_command(l);
                 if (!fn->body) {
                     ast_free((ast_node_t*)fn);
                     return NULL;
                 }
                 
                 fn->base.redirections = parse_redirections(l);
                 return (ast_node_t*)fn;
             }
        }
        
        // Reserved words that terminate a list/command block cannot start a command
        if (is_reserved(t->value, "then") || is_reserved(t->value, "else") || is_reserved(t->value, "elif") || 
            is_reserved(t->value, "fi") || is_reserved(t->value, "do") || is_reserved(t->value, "done") ||
            is_reserved(t->value, "esac") ||
            is_reserved(t->value, "}") || is_reserved(t->value, "!!")) {
            return NULL;
        }
    }
    
    return parse_simple_command(l);
}

static ast_node_t *parse_case(lexer_t *l) {
    token_free(lexer_next(l)); // consume 'case'
    ast_case_t *node = calloc(1, sizeof(ast_case_t));
    node->base.type = NODE_CASE;
    
    token_t *t = lexer_next(l);
    if (!t || t->type != TOKEN_WORD) {
        if (t) token_free(t);
        ast_free((ast_node_t*)node);
        return NULL;
    }
    node->word = strdup(t->value);
    token_free(t);
    
    consume_newlines(l);
    
    t = lexer_next(l);
    if (!t || !is_reserved(t->value, "in")) {
        if (t && is_reserved(t->value, "esac")) {
            // Empty case
            lexer_push_back(l, t);
        } else {
            if (t) token_free(t);
            ast_free((ast_node_t*)node);
            return NULL;
        }
    } else {
        token_free(t);
    }
    consume_newlines(l);
    
    ast_case_item_t *last_item = NULL;
    while (1) {
        t = lexer_peek(l);
        if (!t) break;
        if (is_reserved(t->value, "esac")) {
            token_free(lexer_next(l));
            node->base.redirections = parse_redirections(l);
            return (ast_node_t*)node;
        }
        
        if (t->type == TOKEN_OPERATOR && strcmp(t->value, "(") == 0) {
            token_free(lexer_next(l));
            t = lexer_peek(l);
        }
        
        if (!t || t->type != TOKEN_WORD) break;
        char *pattern = strdup(t->value);
        token_free(lexer_next(l));
        
        t = lexer_next(l);
        if (!t || !t->value || strcmp(t->value, ")") != 0) {
            // Error
            free(pattern);
            ast_free((ast_node_t*)node);
            return NULL;
        }
        token_free(t);
        consume_newlines(l);
        
        ast_node_t *body = parse_list(l);
        
        ast_case_item_t *item = calloc(1, sizeof(ast_case_item_t));
        item->pattern = pattern;
        item->body = body;
        
        if (!node->items) node->items = item;
        else last_item->next = item;
        last_item = item;
        
        t = lexer_peek(l);
        if (t && t->type == TOKEN_OPERATOR && strcmp(t->value, ";;") == 0) {
            token_free(lexer_next(l));
        }
        consume_newlines(l);
    }
    return (ast_node_t*)node;
}


static ast_pipeline_t *create_pipeline(void) {
    ast_pipeline_t *pipe = calloc(1, sizeof(ast_pipeline_t));
    pipe->base.type = NODE_PIPELINE;
    pipe->command_capacity = 4;
    pipe->commands = calloc(pipe->command_capacity, sizeof(ast_node_t*));
    return pipe;
}

static void pipeline_add_command(ast_pipeline_t *pipe, ast_node_t *cmd) {
    if (pipe->command_count >= pipe->command_capacity) {
        pipe->command_capacity *= 2;
        pipe->commands = realloc(pipe->commands, pipe->command_capacity * sizeof(ast_node_t*));
    }
    pipe->commands[pipe->command_count++] = cmd;
}

static ast_node_t *parse_pipeline(lexer_t *l) {
    ast_node_t *left = parse_command(l);
    if (!left) return NULL;

    token_t *t = lexer_peek(l);
    if (t && t->type == TOKEN_OPERATOR && strcmp(t->value, "|") == 0) {
        // It's a pipeline
        ast_pipeline_t *pipe = create_pipeline();
        pipeline_add_command(pipe, left);

        while (1) {
            t = lexer_peek(l);
            if (t && t->type == TOKEN_OPERATOR && strcmp(t->value, "|") == 0) {
                t = lexer_next(l); // consume pipe
                token_free(t);
                
                ast_node_t *right = parse_command(l);
                if (!right) {
                    parser_error(l, "expected command after pipe");
                    ast_free((ast_node_t*)pipe);
                    return NULL;
                }
                pipeline_add_command(pipe, right);
            } else {
                break;
            }
        }
        return (ast_node_t*)pipe;
    }

    return left;
}

static ast_node_t *create_binary_op(list_op_t op, ast_node_t *left, ast_node_t *right) {
    ast_binary_op_t *node = calloc(1, sizeof(ast_binary_op_t));
    node->base.type = NODE_BINARY_OP;
    node->op = op;
    node->left = left;
    node->right = right;
    return (ast_node_t*)node;
}

// Forward decl
static ast_node_t *parse_list(lexer_t *l);

static ast_node_t *parse_logic(lexer_t *l) {
    ast_node_t *left = parse_pipeline(l);
    if (!left) return NULL;

    while (1) {
        token_t *t = lexer_peek(l);
        if (!t || t->type != TOKEN_OPERATOR) break;

        list_op_t op;
        if (strcmp(t->value, "&&") == 0) {
            op = OP_AND;
        } else if (strcmp(t->value, "||") == 0) {
            op = OP_OR;
        } else {
            break;
        }

        t = lexer_next(l); // consume op
        token_free(t);

        ast_node_t *right = parse_pipeline(l);
        if (!right) {
            parser_error(l, "expected command after logical operator");
            ast_free(left);
            return NULL;
        }

        left = create_binary_op(op, left, right);
    }
    return left;
}

static ast_node_t *parse_list(lexer_t *l) {
    consume_newlines(l);
    ast_node_t *left = parse_logic(l);
    if (!left) return NULL;

    while (1) {
        token_t *t = lexer_peek(l);
        if (!t) break;
        
        // Handle background operator &
        if (t->type == TOKEN_OPERATOR && strcmp(t->value, "&") == 0) {
            token_free(lexer_next(l)); // consume &
            consume_newlines(l);
            
            // Wrap left in a background node with NULL right (unary background)
            left = create_binary_op(OP_BACKGROUND, left, NULL);
            
            // Check if we hit a reserved word that should terminate the list
            token_t *next_t = lexer_peek(l);
            if (!next_t || next_t->type == TOKEN_EOF) break;
            if (next_t->type == TOKEN_WORD && (
                is_reserved(next_t->value, "done") || is_reserved(next_t->value, "fi") || 
                is_reserved(next_t->value, "else") || is_reserved(next_t->value, "elif") ||
                is_reserved(next_t->value, "esac") || is_reserved(next_t->value, "}") ||
                is_reserved(next_t->value, ")"))) {
                break;
            }
            
            // Parse next command in sequence
            ast_node_t *right = parse_logic(l);
            if (right) {
                left = create_binary_op(OP_SEQ, left, right);
            } else {
                break;
            }
        }
        else if (t->type == TOKEN_NEWLINE || (t->type == TOKEN_OPERATOR && strcmp(t->value, ";") == 0)) {
            token_free(lexer_next(l)); // consume separator
            consume_newlines(l);
            
            // Check if we hit a reserved word that should terminate the list
            token_t *next_t = lexer_peek(l);
            if (next_t && next_t->type == TOKEN_WORD && (
                is_reserved(next_t->value, "done") || is_reserved(next_t->value, "fi") || 
                is_reserved(next_t->value, "else") || is_reserved(next_t->value, "elif") ||
                is_reserved(next_t->value, "esac") || is_reserved(next_t->value, "}") ||
                is_reserved(next_t->value, ")"))) {
                break;
            }
            
            ast_node_t *right = parse_logic(l);
            if (right) {
                left = create_binary_op(OP_SEQ, left, right);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    return left;
}

ast_node_t *parser_parse(lexer_t *l) {
    consume_newlines(l);
    token_t *t = lexer_peek(l);
    if (!t || t->type == TOKEN_EOF) return NULL;
    
    ast_node_t *node = parse_list(l);
    if (!node && lexer_peek(l)->type != TOKEN_EOF) {
        parser_error(l, "unexpected list match failure");
    }
    return node;
}
