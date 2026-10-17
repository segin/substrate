/* a nullptr_t value casts only to void, bool, a pointer or nullptr_t (C23 6.5.5) */
int f(void) { return (int)nullptr; }
