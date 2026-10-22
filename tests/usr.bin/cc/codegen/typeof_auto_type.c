/*
 * `typeof` of an `__auto_type` variable, and of one initialized from a
 * statement expression.  Linux's scoped user-access macros declare
 * `const __auto_type dirent = _tmpptr` in a `for` initializer, where
 * _tmpptr is itself an `__auto_type` set from `({ ...; __retptr; })`, then
 * take `__typeof__(&dirent->d_ino)`.  The parser settles typeof as it
 * reads it but left an `__auto_type` variable's type to sema, and could not
 * type a statement expression whose value names the block's own locals,
 * so cc stopped with "cannot determine the type of typeof's operand".
 */
#include <stdio.h>

struct d { long ino; char name[8]; };
static struct d g = { 42, "x" };

static int plain(void) {
    __auto_type p = &g;
    __typeof__(p->ino) v = p->ino;

    return (int)v + (int)sizeof(v);
}

static int from_stmt_expr(void) {
    __auto_type q = ({ struct d *r = &g; r; });
    __typeof__(*(&q->ino)) v = 1;

    return (int)(v + q->ino);
}

static int in_for_init(void) {
    int s = 0;

    for (__auto_type tp = ({ struct d *x = &g; x; }); s == 0; s = 1) {
        for (const __auto_type p = tp; s == 0; s = 2) {
            __typeof__((&p->ino)) pp = &p->ino;
            s += (int)*pp;
        }
    }
    return s;
}

static int array_decays(void) {
    int a[3] = { 1, 2, 3 };
    __auto_type ap = a;                 /* int *, not int[3] */
    __typeof__(ap) bp = ap + 1;

    return (int)sizeof(ap) + *bp;
}

int main(void) {
    printf("%d %d %d %d\n", plain(), from_stmt_expr(), in_for_init(), array_decays());
    return 0;
}
