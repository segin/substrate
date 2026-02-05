#include "lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void lexer_init(lexer_t *l, const char *input) {
    l->input = input;
    l->pos = 0;
    l->len = strlen(input);
    l->error = 0;
    l->lookahead[0] = NULL;
    l->lookahead[1] = NULL;
    l->lookahead[2] = NULL;
    l->lookahead[3] = NULL;
    l->lookahead_count = 0;
}

static char peek(lexer_t *l) {
    if (l->pos >= l->len) return 0;
    return l->input[l->pos];
}

static char peek_n(lexer_t *l, int n) {
    if (l->pos + n >= l->len) return 0;
    return l->input[l->pos + n];
}

static char advance(lexer_t *l) {
    if (l->pos >= l->len) return 0;
    return l->input[l->pos++];
}

static int is_operator_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}

static int is_meta_char(char c) {
    return isspace(c) || is_operator_char(c);
}

// Check for operators
// Returns 1 if operator found and consumed (and returns token), 0 otherwise
static token_t *scan_operator(lexer_t *l) {
    char c = peek(l);
    if (!is_operator_char(c)) return NULL;

    token_t *tok = calloc(1, sizeof(token_t));
    tok->type = TOKEN_OPERATOR;
    char val[4] = {0};

    // Handle multi-char operators
    // && || ;; << >> <> >| 
    // And simplistic ones like > < & | ; ( )

    char next = peek_n(l, 1);
    
    // Check 2-char ops
    if ((c == '&' && next == '&') ||
        (c == '|' && next == '|') ||
        (c == ';' && next == ';') ||
        (c == '<' && next == '<') ||
        (c == '>' && next == '>') ||
        (c == '<' && next == '>') ||
        (c == '>' && next == '|') ||
        (c == '<' && next == '&') ||
        (c == '>' && next == '&')) {
        
        val[0] = advance(l);
        val[1] = advance(l);
        tok->value = strdup(val);
        return tok;
    }

    // Single char
    val[0] = advance(l);
    tok->value = strdup(val);
    return tok;
}

static void buffer_append(char **buf, size_t *cap, size_t *len, char c) {
    if (*len + 1 >= *cap) {
        *cap *= 2;
        if (*cap == 0) *cap = 16;
        *buf = realloc(*buf, *cap);
    }
    (*buf)[*len] = c;
    (*len)++;
    (*buf)[*len] = 0;
}

static token_t *lexer_scan(lexer_t *l) {
    // (Scanning logic starts here)

    while (l->pos < l->len) {
        char c = peek(l);

        // Discard backslash-newline (Line continuation)
        if (c == '\\' && peek_n(l, 1) == '\n') {
            advance(l);
            advance(l);
            continue;
        }

        // Skip whitespace (EXCEPT newline)
        if (isspace(c) && c != '\n') {
            advance(l);
            continue;
        }

        if (c == '\n') {
            advance(l);
            token_t *t = calloc(1, sizeof(token_t));
            t->type = TOKEN_NEWLINE;
            t->value = strdup("\n");
            return t;
        }

        if (c == '#') {
            // Comment
            while (peek(l) && peek(l) != '\n') advance(l);
            continue; // Loop back to handle newline or EOF
        }

        // Try IO Number (digit followed by < or >)
        // Only if it looks like a number start and is purely digits until operator
        // POSIX: IO_NUMBER is only recognized if it is entirely digits and immediately followed by < or >
        if (isdigit(c)) {
            // Must check if this digit is part of a word or an IO number
            // Look ahead to see if it's digit+ followed by < or >
            size_t k = 0;
            while (isdigit(peek_n(l, k))) k++;
            char after = peek_n(l, k);
            if (after == '<' || after == '>') {
                // It is an IO number
                size_t cap = 8;
                size_t len = 0;
                char *buf = malloc(cap);
                buf[0] = 0;
                for (size_t i = 0; i < k; i++) {
                    buffer_append(&buf, &cap, &len, advance(l));
                }
                token_t *t = calloc(1, sizeof(token_t));
                t->type = TOKEN_IO_NUMBER;
                t->value = buf;
                return t;
            }
        }

        // Try Operator
        token_t *op = scan_operator(l);
        if (op) return op;

        // Word
        // Read until meta-char, but handle quoting / escaping
        size_t cap = 32;
        size_t len = 0;
        char *buf = malloc(cap);
        buf[0] = 0;

        int in_sq = 0; // single quote
        int in_dq = 0; // double quote
        int escape = 0;
        int was_quoted = 0;
        int paren_depth = 0;
        int backtick = 0;
        int last_was_dollar = 0;

        while (l->pos < l->len) {
            c = peek(l);

            if (escape) {
                was_quoted = 1;
                if (c == '\n') {
                    // Line continuation: discard both
                    advance(l);
                    escape = 0;
                    continue;
                }
                
                // In double quotes, only specific characters are escaped
                if (in_dq) {
                    if (c == '$' || c == '`' || c == '"' || c == '\\') {
                        buffer_append(&buf, &cap, &len, c);
                    } else {
                        // Backslash was literal if NOT one of the above
                        buffer_append(&buf, &cap, &len, '\\');
                        buffer_append(&buf, &cap, &len, c);
                    }
                } else {
                    // Normal escape preserves character literally
                    buffer_append(&buf, &cap, &len, c);
                }
                
                advance(l);
                escape = 0;
                continue;
            }

            if (in_sq) {
                if (c == '\'') {
                    in_sq = 0;
                    buffer_append(&buf, &cap, &len, c);
                } else {
                    buffer_append(&buf, &cap, &len, c);
                }
                advance(l);
                continue;
            }

            if (in_dq) {
                if (c == '\\') {
                    char next = peek_n(l, 1);
                    if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') {
                        escape = 1;
                        advance(l);
                        continue;
                    }
                }
                if (c == '`') backtick = !backtick;
                if (c == '"') in_dq = 0;
                buffer_append(&buf, &cap, &len, c);
                advance(l);
                continue;
            }

            // Normal mode
            if (c == '\\') {
                if (paren_depth > 0 || backtick) {
                    // Inside an expansion, preserve the backslash
                    buffer_append(&buf, &cap, &len, c);
                    advance(l);
                    // But we might need to handle the next char specially too
                    // Actually, just peek and append the next one too
                    c = peek(l);
                    if (c) {
                        buffer_append(&buf, &cap, &len, c);
                        advance(l);
                    }
                    continue;
                }
                escape = 1;
                advance(l);
                continue;
            }

            if (c == '\'') {
                in_sq = 1;
                was_quoted = 1;
                buffer_append(&buf, &cap, &len, c);
                advance(l);
                continue;
            }

            if (c == '"') {
                in_dq = 1;
                was_quoted = 1;
                buffer_append(&buf, &cap, &len, c);
                advance(l);
                continue;
            }

            if (c == '`') {
                backtick = !backtick;
                was_quoted = 1;
                buffer_append(&buf, &cap, &len, c);
                advance(l);
                continue;
            }

            if (is_meta_char(c)) {
                // Paren balancing for $(...)
                if (c == '(') {
                    if (last_was_dollar || paren_depth > 0) {
                        paren_depth++;
                        buffer_append(&buf, &cap, &len, c);
                        advance(l);
                        last_was_dollar = 0;
                        continue;
                    }
                } else if (c == ')') {
                    if (paren_depth > 0) {
                        paren_depth--;
                        buffer_append(&buf, &cap, &len, c);
                        advance(l);
                        last_was_dollar = 0;
                        continue;
                    }
                }

                if (paren_depth > 0 || backtick) {
                    buffer_append(&buf, &cap, &len, c);
                    advance(l);
                    last_was_dollar = (c == '$');
                    continue;
                }

                break; // End of word
            }

            buffer_append(&buf, &cap, &len, c);
            advance(l);
            last_was_dollar = (c == '$');
        }
        
        // Handle unclosed quotes?
        if (in_sq || in_dq) {
             l->error = 1;
             // Return error token?
             free(buf);
             token_t *t = calloc(1, sizeof(token_t));
             t->type = TOKEN_ERROR;
             t->value = strdup("Unclosed quote");
             return t;
        }

        token_t *t = calloc(1, sizeof(token_t));
        t->type = TOKEN_WORD;
        t->value = buf;
        t->quoted = was_quoted;
        return t;
    }

    token_t *t = calloc(1, sizeof(token_t));
    t->type = TOKEN_EOF;
    return t;
}

token_t *lexer_next(lexer_t *l) {
    if (l->lookahead_count > 0) {
        token_t *t = l->lookahead[0];
        for (int i = 0; i < l->lookahead_count - 1; i++) {
            l->lookahead[i] = l->lookahead[i+1];
        }
        l->lookahead[l->lookahead_count - 1] = NULL;
        l->lookahead_count--;
        return t;
    }
    return lexer_scan(l);
}

token_t *lexer_peek(lexer_t *l) {
    if (l->lookahead_count > 0) {
        return l->lookahead[0];
    }
    token_t *t = lexer_scan(l);
    l->lookahead[0] = t;
    l->lookahead_count = 1;
    return t;
}

// Support for peeking n tokens (0-indexed)
token_t *lexer_peek_n(lexer_t *l, int n) {
    if (n >= 4) return NULL;
    while (l->lookahead_count <= n) {
        l->lookahead[l->lookahead_count++] = lexer_scan(l);
    }
    return l->lookahead[n];
}

token_t *lexer_peek2(lexer_t *l) {
    return lexer_peek_n(l, 1);
}

void lexer_push_back(lexer_t *l, token_t *t) {
    if (l->lookahead_count >= 4) {
        token_free(t); // No space
        return;
    }
    for (int i = l->lookahead_count; i > 0; i--) {
        l->lookahead[i] = l->lookahead[i-1];
    }
    l->lookahead[0] = t;
    l->lookahead_count++;
}

void lexer_clear_lookahead(lexer_t *l) {
    for (int i = 0; i < l->lookahead_count; i++) {
        token_free(l->lookahead[i]);
        l->lookahead[i] = NULL;
    }
    l->lookahead_count = 0;
}
