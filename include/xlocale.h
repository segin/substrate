/*
 * <xlocale.h> — BSD per-thread locale extension.
 * Substrate exposes the POSIX locale_t API through <locale.h>; this
 * header is here so build systems that probe for xlocale.h on BSD
 * targets still find one.
 */
#ifndef _XLOCALE_H
#define _XLOCALE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <locale.h>
#ifdef __cplusplus
}
#endif
#endif
