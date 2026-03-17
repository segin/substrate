with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

import re

# `push_stmt_arr` can't be easily modified directly because we need capacity.
# Let's change its signature to take a `size_t *cap` parameter.

# Find all calls to `push_stmt_arr(&some_arr, &some_count, ...)`
# For `cc_stmt_t s`, there's `s.block_stmts`, `s.block_count`. We added `s.block_cap`.
# So we can replace `push_stmt_arr(&s->block_stmts, &s->block_count, child)` with `push_stmt_arr(&s->block_stmts, &s->block_count, &s->block_cap, child)`

def modify_parser():
    with open("usr.bin/cc/frontend/parser.c", "r") as f:
        content = f.read()

    # 1. Modify `struct_member_push`
    struct_member_push_old = """    if (sd->member_count == sd->member_cap) {
        size_t ncap = sd->member_cap == 0 ? 8 : sd->member_cap * 2;
        next = (cc_struct_member_t *)realloc(sd->members, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        sd->members = next;
        sd->member_cap = ncap;
    }"""
    # Wait, `struct_member_push` already uses geometric allocation!
    pass

modify_parser()
