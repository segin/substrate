#pragma STDC FP_CONTRACT ON
#pragma STDC FENV_ACCESS OFF
#pragma STDC CX_LIMITED_RANGE DEFAULT

static int f(void) {
    return 7;
}

int main(void) {
    return f() - 7;
}
