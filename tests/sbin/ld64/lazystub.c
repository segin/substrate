/*
 * lazystub.c - the definition of ld64_absent() that lazy is LINKED
 * against (libld64lazy-link.so, never installed).  The libld64lazy.so
 * installed on the target is built without it, which leaves the program
 * with a PLT slot for a function that does not exist at run time.
 */

void ld64_absent(void) {}
