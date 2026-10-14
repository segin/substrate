__extension__ extern int extdecl(__const char *__restrict s) __asm__ ("" "__extdecl_alias");

int main(void) {
    char *p = 0;
    return p == 0 ? 0 : 1;
}
