/*
 * A parameter may have the name of a typedef: after the declaration
 * specifiers an identifier is the declarator's name, so `poll_table
 * *poll_table` declares a parameter poll_table (Linux's <linux/poll.h>
 * and <linux/ring_buffer.h> have exactly this).  cc took the second
 * poll_table for a type and stopped with "expected ')' after parameter
 * list".  `int (T)` is the one ambiguous form, and there T is the type
 * (C11 6.7.6.3p11): a function taking a T.
 */
#include <stdio.h>

typedef struct pt { int key; } poll_table;
typedef int count;

int decl_only(int full, poll_table *poll_table, int n);

static int by_ptr(poll_table *poll_table) { return poll_table->key; }
static int by_val(count count) { return count + 1; }
static int ptr_in_parens(count (*count)) { return *count * 2; }
static int fn_param(int (count)) { return 0; }  /* count is a type here */

static int call_fn(int (*fp)(count)) { return fp(5); }
static int five(count c) { return c; }

int main(void) {
    poll_table p = { 41 };
    count c = 20;

    printf("%d %d %d %d\n", by_ptr(&p), by_val(6), ptr_in_parens(&c), call_fn(five));
    printf("%d\n", fn_param(0) + (int)sizeof(int (*)(count)));
    return 0;
}
