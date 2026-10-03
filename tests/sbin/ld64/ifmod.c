/*
 * ifmod.c - libld64if.so, a shared object exporting an indirect function
 * for ifunc.c.  ifmod_pick is an STT_GNU_IFUNC symbol: its value is the
 * resolver below, and its address is whatever the resolver returns.
 *
 * The resolver reads ifmod_selector through the GOT, so it only returns
 * the right implementation if this object was relocated before the
 * linker called it.
 */

int ifmod_selector = 2;
int ifmod_resolver_runs;

static int pick_one(int x) { return x + 100; }
static int pick_two(int x) { return x + 200; }

static int (*resolve_pick(void))(int) {
    ifmod_resolver_runs++;
    return ifmod_selector == 2 ? pick_two : pick_one;
}

int ifmod_pick(int x) __attribute__((ifunc("resolve_pick")));

/* A call from inside the library: -Bsymbolic-functions is not in effect
 * for these tests' libraries, so this is a JUMP_SLOT against the
 * indirect function in the library's own PLT. */
int ifmod_twice(int x) { return ifmod_pick(ifmod_pick(x)); }
