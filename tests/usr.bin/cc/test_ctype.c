/*
 * test_ctype.c: the interned C type graph (usr.bin/cc/frontend/ctype.c):
 * identity, spelling, size and alignment on both targets, qualifiers and
 * decay.  Expected spellings are clang's (gcc writes `char * const`);
 * expected sizes are gcc's.
 */
#include "cc_ctype.h"
#include "cc_target.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

/**
 * Check that a type is spelled as expected.
 *
 * @param t     the type
 * @param want  gcc's spelling of it
 * @param line  the caller's line, for the message
 */
static void check_spelling(const cc_ctype_t *t, const char *want, int line) {
    char buf[256];

    cc_ct_format(t, buf, sizeof(buf));
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "FAIL %s:%d: spelled \"%s\", want \"%s\"\n", __FILE__, line, buf, want);
        ++failures;
    }
}

#define SPELL(t, want) check_spelling((t), (want), __LINE__)

/**
 * Run every check, printing each failure.
 *
 * @return 0 if all passed, else 1
 */
int main(void) {
    const cc_ctype_t *i32 = cc_ct_basic(CC_CT_INT);
    const cc_ctype_t *ch = cc_ct_basic(CC_CT_CHAR);
    const cc_ctype_t *vd = cc_ct_basic(CC_CT_VOID);
    const cc_ctype_t *pint = cc_ct_pointer(i32);
    const cc_ctype_t *arr_of_ptr = cc_ct_array(pint, 7);              /* int *[7] */
    const cc_ctype_t *ptr_to_arr = cc_ct_pointer(cc_ct_array(i32, 7)); /* int (*)[7] */
    const cc_ctype_t *ic[2];
    const cc_ctype_t *fn_ic;
    const cc_ctype_t *fn_ret_ptr;
    const cc_ctype_t *nested;
    const cc_ctype_t *cint = cc_ct_qualified(i32, CC_CQ_CONST);
    cc_translation_unit_t tu;
    cc_struct_def_t sd;

    /* identity: the same type is the same node, different types are not */
    CHECK(cc_ct_pointer(i32) == pint);
    CHECK(cc_ct_array(pint, 7) == arr_of_ptr);
    CHECK(arr_of_ptr != ptr_to_arr);
    CHECK(cc_ct_pointer(pint) != ptr_to_arr);
    ic[0] = i32;
    ic[1] = ch;
    fn_ic = cc_ct_function(i32, ic, 2, 0, 1);
    CHECK(cc_ct_function(i32, ic, 2, 0, 1) == fn_ic);
    CHECK(cc_ct_function(i32, ic, 2, 1, 1) != fn_ic);
    CHECK(cc_ct_function(i32, NULL, 0, 0, 0) != cc_ct_function(i32, NULL, 0, 0, 1));
    fn_ret_ptr = cc_ct_function(pint, NULL, 0, 0, 1);
    CHECK(cc_ct_pointer(fn_ret_ptr) != cc_ct_pointer(pint)); /* int *(*)(void) is not int ** */

    /* spelling */
    SPELL(i32, "int");
    SPELL(pint, "int *");
    SPELL(arr_of_ptr, "int *[7]");
    SPELL(ptr_to_arr, "int (*)[7]");
    SPELL(cc_ct_array(cc_ct_array(i32, 3), 2), "int[2][3]");
    SPELL(cc_ct_pointer(fn_ic), "int (*)(int, char)");
    SPELL(cc_ct_pointer(fn_ret_ptr), "int *(*)(void)");
    SPELL(cc_ct_function(i32, NULL, 0, 0, 0), "int()");
    SPELL(cc_ct_function(i32, ic, 1, 1, 1), "int(int, ...)");
    nested = cc_ct_pointer(cc_ct_function(cc_ct_pointer(cc_ct_function(vd, NULL, 0, 0, 1)), ic, 1, 0, 1));
    SPELL(nested, "void (*(*)(int))(void)");
    SPELL(cc_ct_pointer(cint), "const int *");
    SPELL(cc_ct_qualified(cc_ct_pointer(ch), CC_CQ_CONST), "char *const");
    SPELL(cc_ct_bitint(13, 0), "_BitInt(13)");
    SPELL(cc_ct_complex(CC_CT_COMPLEX, cc_ct_basic(CC_CT_DOUBLE)), "_Complex double");

    /* qualifiers: part of identity; on an array they qualify the elements */
    CHECK(cint != i32);
    CHECK(cint->unqual == i32);
    CHECK(cc_ct_qualified(cint, CC_CQ_CONST) == cint);
    CHECK(cc_ct_qualified(cc_ct_array(i32, 3), CC_CQ_CONST) == cc_ct_array(cint, 3));

    /* decay */
    CHECK(cc_ct_decay(cc_ct_array(i32, 3)) == pint);
    CHECK(cc_ct_decay(fn_ic) == cc_ct_pointer(fn_ic));
    CHECK(cc_ct_decay(cint) == i32);

    /* predicates */
    CHECK(cc_ct_is_integer(cc_ct_enum(-1)) && cc_ct_is_integer(cc_ct_basic(CC_CT_BOOL)));
    CHECK(cc_ct_is_unsigned(cc_ct_basic(CC_CT_BOOL)) && !cc_ct_is_unsigned(i32));
    CHECK(cc_ct_is_scalar(pint) && !cc_ct_is_scalar(arr_of_ptr) && !cc_ct_is_arithmetic(pint));
    CHECK(cc_ct_is_floating(cc_ct_basic(CC_CT_LDOUBLE)));

    /* sizes: x86-64 */
    memset(&tu, 0, sizeof(tu));
    memset(&sd, 0, sizeof(sd));
    sd.size = 24;
    sd.align = 8;
    sd.complete = 1;
    tu.structs = &sd;
    tu.struct_count = 1;
    cc_target_select(CC_TARGET_X86_64);
    CHECK(cc_ct_size(arr_of_ptr, &tu) == 56);
    CHECK(cc_ct_size(ptr_to_arr, &tu) == 8);
    CHECK(cc_ct_size(cc_ct_array(cc_ct_array(i32, 3), 2), &tu) == 24);
    CHECK(cc_ct_size(cc_ct_basic(CC_CT_LDOUBLE), &tu) == 16);
    CHECK(cc_ct_size(fn_ic, &tu) == 1);
    CHECK(cc_ct_size(vd, &tu) == -1);
    CHECK(cc_ct_size(cc_ct_array(i32, -1), &tu) == -1);
    CHECK(cc_ct_size(cc_ct_array(cc_ct_record(0), 2), &tu) == 48);
    CHECK(cc_ct_align(cc_ct_array(cc_ct_record(0), 2), &tu) == 8);
    CHECK(cc_ct_size(cc_ct_record(1), &tu) == -1);
    CHECK(cc_ct_size(cc_ct_bitint(13, 0), &tu) == 2 && cc_ct_size(cc_ct_bitint(65, 1), &tu) == 16);
    CHECK(cc_ct_size(cc_ct_complex(CC_CT_COMPLEX, cc_ct_basic(CC_CT_FLOAT)), &tu) == 8);

    /* sizes: i386 */
    cc_target_select(CC_TARGET_I386);
    CHECK(cc_ct_size(arr_of_ptr, &tu) == 28);
    CHECK(cc_ct_size(ptr_to_arr, &tu) == 4);
    CHECK(cc_ct_size(cc_ct_basic(CC_CT_LDOUBLE), &tu) == 12);
    CHECK(cc_ct_align(cc_ct_basic(CC_CT_LLONG), &tu) == 4);

    /* a reset forgets everything, and building again still works */
    cc_ct_reset();
    i32 = cc_ct_basic(CC_CT_INT);
    CHECK(i32 != NULL && cc_ct_pointer(i32) == cc_ct_pointer(i32));
    cc_ct_reset();

    if (failures == 0) {
        printf("test_ctype: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
