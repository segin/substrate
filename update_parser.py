import re

with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

# Let's fix parse_decl_stmt_list first. We just need to add a `size_t *cap` param, or we can just use realloc safely inside parse_decl_stmt_list by changing its signature to `size_t *cap`.
# Wait, `parse_decl_stmt_list` modifies `arr` which is often dynamically allocated.

# If we don't want to change ALL these signatures, we can do the simple geometric growth by keeping track of the capacity dynamically, but without storing the cap, we can't do it in O(1) space. Wait! We CAN just use `_cap` variables in the structs!

text = text.replace(
    "static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi) {",
    "static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, size_t *cap, int need_semi) {"
)

# And inside parse_decl_stmt_list:
text = text.replace(
    "if (push_stmt_arr(arr, count, s) != 0) {",
    "if (push_stmt_arr(arr, count, cap, s) != 0) {"
)

# And all calls to parse_decl_stmt_list:
text = re.sub(
    r'parse_decl_stmt_list\(([^,]+), ([^,]+), ([^,]+), ([^)]+)\)',
    r'parse_decl_stmt_list(\1, \2, \3, \3_cap_replace, \4)',
    text
)
# We have to fix `\3_cap_replace` manually where it is used.
with open("usr.bin/cc/frontend/parser.c", "w") as f:
    f.write(text)
