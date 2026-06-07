#include <ctype.h>

int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isalpha(int c) { return islower(c) || isupper(c); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isgraph(int c) { return c >= 33 && c <= 126; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c <= 126; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }


int isascii(int c) { return (unsigned)c < 128; }
int toascii(int c) { return c & 0x7f; }

/*
 * glibc ctype internals.  Code compiled against glibc headers (ksh93's libast)
 * implements the is*() macros as table lookups into the array returned by
 * __ctype_b_loc(): `(*__ctype_b_loc())[c]` indexed by an int in [-128,255].
 * Build that table once with the per-class bit values glibc uses and hand back
 * a pointer biased by 128 so negative indices (EOF) are valid.
 */
#define _ISbit(bit) ((bit) < 8 ? ((1 << (bit)) << 8) : ((1 << (bit)) >> 8))
enum {
    _ISupper  = _ISbit(0),  _ISlower  = _ISbit(1),
    _ISalpha  = _ISbit(2),  _ISdigit  = _ISbit(3),
    _ISxdigit = _ISbit(4),  _ISspace  = _ISbit(5),
    _ISprint  = _ISbit(6),  _ISgraph  = _ISbit(7),
    _IScntrl  = _ISbit(8),  _ISpunct  = _ISbit(9),
    _ISalnum  = _ISbit(10), _ISblank  = _ISbit(11)
};

static unsigned short __ctype_b_table[384];
static int            __ctype_b_built;

static void __ctype_b_build(void)
{
    for (int i = 0; i < 256; i++) {
        unsigned short f = 0;
        if (isupper(i))  f |= _ISupper;
        if (islower(i))  f |= _ISlower;
        if (isalpha(i))  f |= _ISalpha;
        if (isdigit(i))  f |= _ISdigit;
        if (isxdigit(i)) f |= _ISxdigit;
        if (isspace(i))  f |= _ISspace;
        if (isprint(i))  f |= _ISprint;
        if (isgraph(i))  f |= _ISgraph;
        if (iscntrl(i))  f |= _IScntrl;
        if (ispunct(i))  f |= _ISpunct;
        if (isalnum(i))  f |= _ISalnum;
        if (isblank(i))  f |= _ISblank;
        __ctype_b_table[128 + i] = f;
    }
    __ctype_b_built = 1;
}

const unsigned short **__ctype_b_loc(void)
{
    static const unsigned short *ptr;
    if (!__ctype_b_built)
        __ctype_b_build();
    ptr = &__ctype_b_table[128];
    return &ptr;
}

/* MB_CUR_MAX accessor — substrate is a single-byte "C" locale. */
int __ctype_get_mb_cur_max(void)
{
    return 1;
}
