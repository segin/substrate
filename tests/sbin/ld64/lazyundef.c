/*
 * lazyundef.c - libld64undef.so, a shared object with a PLT slot for a
 * function nothing defines.  It loads when bound lazily and must be
 * refused by dlopen(RTLD_NOW); lazyundef_ok() works either way because
 * it never makes the call.
 */

extern void ld64_absent_from_module(void);

int lazyundef_ok(void) { return 7; }

void lazyundef_call(void) { ld64_absent_from_module(); }
