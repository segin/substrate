/*
 * ifunc.c - indirect functions under /sbin/ld64.so.
 *
 *   exe_pick     a static indirect function in the executable: the link
 *                editor resolves the reference locally and leaves an
 *                R_X86_64_IRELATIVE for the linker
 *   ifmod_pick   an STT_GNU_IFUNC symbol exported by libld64if.so, called
 *                (JUMP_SLOT), address-taken (GLOB_DAT) and looked up with
 *                dlsym - all three must give the implementation, never
 *                the resolver
 */
#include <dlfcn.h>
#include <stdio.h>

extern int ifmod_pick(int);
extern int ifmod_twice(int);
extern int ifmod_resolver_runs;

static int exe_impl(int x) { return x * 3; }
static int exe_resolver_runs;

static int (*resolve_exe(void))(int) {
    exe_resolver_runs++;
    return exe_impl;
}

static int exe_pick(int x) __attribute__((ifunc("resolve_exe")));

int main(void) {
    int bad = 0;

    int a = exe_pick(14);
    printf("exe_pick(14)=%d resolver runs=%d\n", a, exe_resolver_runs);
    if (a != 42 || exe_resolver_runs < 1) bad = 1;

    int b = ifmod_pick(1);
    int c = ifmod_twice(1);
    printf("ifmod_pick(1)=%d ifmod_twice(1)=%d resolver runs=%d\n",
           b, c, ifmod_resolver_runs);
    if (b != 201 || c != 401 || ifmod_resolver_runs < 1) bad = 1;

    int (*fp)(int) = ifmod_pick;
    int (*dp)(int) = (int (*)(int))dlsym(RTLD_DEFAULT, "ifmod_pick");
    printf("&ifmod_pick %s dlsym, fp(2)=%d dp(3)=%d\n",
           fp == dp ? "==" : "!=", fp(2), dp ? dp(3) : -1);
    if (!dp || fp != dp || fp(2) != 202 || dp(3) != 203) bad = 1;

    printf("ifunc: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
