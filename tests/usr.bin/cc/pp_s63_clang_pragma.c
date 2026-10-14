#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang attribute push (__attribute__((unused)), apply_to = function)
#pragma clang loop unroll(enable)
#pragma clang section text=".tsec" data=".dsec" bss=".bsec"
#pragma clang fp contract(on)
#pragma clang attribute pop
#pragma clang diagnostic pop
int clang_pragma_ok = 11;
