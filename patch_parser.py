with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

# Replace push_stmt_arr signature and usage
# First, update definition
import re
text = re.sub(r'static int push_stmt_arr\(cc_stmt_t \*\*arr, size_t \*count, cc_stmt_t s\)', 'static int push_stmt_arr(cc_stmt_t **arr, size_t *count, size_t *cap, cc_stmt_t s)', text)

# Then update implementation
push_stmt_arr_old = """    cc_stmt_t *next = (cc_stmt_t *)realloc(*arr, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *arr = next;
    (*arr)[(*count)++] = s;
    return 0;
}"""

push_stmt_arr_new = """    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : *cap * 2;
        cc_stmt_t *next = (cc_stmt_t *)realloc(*arr, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *arr = next;
        *cap = ncap;
    }
    (*arr)[(*count)++] = s;
    return 0;
}"""

text = text.replace(push_stmt_arr_old, push_stmt_arr_new)

# Update push_stmt_func
push_stmt_func_old = """static int push_stmt_func(cc_function_t *f, cc_stmt_t s) {
    return push_stmt_arr(&f->stmts, &f->stmt_count, s);
}"""

push_stmt_func_new = """static int push_stmt_func(cc_function_t *f, cc_stmt_t s) {
    return push_stmt_arr(&f->stmts, &f->stmt_count, &f->stmt_cap, s);
}"""

text = text.replace(push_stmt_func_old, push_stmt_func_new)

# Update all calls to push_stmt_arr
text = re.sub(r'push_stmt_arr\(&s->block_stmts, &s->block_count, child\)', r'push_stmt_arr(\&s->block_stmts, \&s->block_count, \&s->block_cap, child)', text)
text = re.sub(r'push_stmt_arr\(&s->block_stmts, &s->block_count, label_stmt\)', r'push_stmt_arr(\&s->block_stmts, \&s->block_count, \&s->block_cap, label_stmt)', text)
text = re.sub(r'push_stmt_arr\(&s->block_stmts, &s->block_count, tail_stmt\)', r'push_stmt_arr(\&s->block_stmts, \&s->block_count, \&s->block_cap, tail_stmt)', text)
text = re.sub(r'push_stmt_arr\(arr, count, s\)', r'push_stmt_arr(arr, count, cap, s)', text)
# Wait! parse_decl_stmt_list takes arr and count. Let's see its signature.
# static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi)
