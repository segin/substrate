with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

# push_asm_operand
text = text.replace("""static int push_asm_operand(cc_asm_operand_t **items, size_t *count, cc_asm_operand_t *item) {
    cc_asm_operand_t *next;
    if (items == NULL || count == NULL || item == NULL) {
        return -1;
    }
    next = (cc_asm_operand_t *)realloc(*items, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *items = next;
    (*items)[*count] = *item;
    (*count)++;
    return 0;
}""",
"""static int push_asm_operand(cc_asm_operand_t **items, size_t *count, cc_asm_operand_t *item) {
    cc_asm_operand_t *next;
    if (items == NULL || count == NULL || item == NULL) {
        return -1;
    }
    # No capacity tracked for asm operands in struct cc_stmt, we'll leave it linear since it's small,
    # but wait, let's see where it's used.
""")
# Instead of modifying small linearly grown arrays that don't cause major performance issues,
# let's just stick to the important ones! The major ones are the ones we just did:
# tu_funcs, p_typedefs, p_vars, p_enum_consts, p_enum_tags, p_structs, sd_members, args, params, stmts, globals.
