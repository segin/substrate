/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Berkeley Software Distribution.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cdefs.h	8.8 (Berkeley) 1/21/94
 * $FreeBSD$
 */

#ifndef _SYS_CDEFS_H_
#define	_SYS_CDEFS_H_

/*
 * Macro for wrapping declarations in C++ compatibility.
 */
#ifdef __cplusplus
#define	__BEGIN_DECLS	extern "C" {
#define	__END_DECLS	}
#else
#define	__BEGIN_DECLS
#define	__END_DECLS
#endif

/*
 * The __CONCAT macro is used to concatenate parts of symbol names.  This is
 * hidden in this header for two reasons:  it makes the actual code more
 * readable when doing a large number of __CONCAT etc. operations, and it makes
 * the meaning of triple-nested __CONCAT's as used in sh.h difficult to parse
 * when casually looking over code.
 */
#if defined(__GNUC__) || defined(__INTEL_COMPILER)
#define	__CONCAT(x,y)	x ## y
#define	__XCONCAT(x,y)	__CONCAT(x,y)
#else
#define	__CONCAT(x,y)	x/**/y
#define	__XCONCAT(x,y)	__CONCAT(x,y)
#endif

/*
 * GCC does allow __typeof to be used in all cases; other compilers use it
 * conditionally.
 */
#if !defined(__GNUC__)
#define	__typeof(x)	void *
#endif

/*
 * Compiler-dependent macros to declare that functions are a) non-returning
 * or b) printf-like, possibly with arguments that are checked against
 * printf-style format strings.
 */
#if (__GNUC__ == 2 && __GNUC_MINOR__ >= 5) || __GNUC__ > 2 || defined(__clang__)
#define	__dead		__attribute__((__noreturn__))
#define	__pure		__attribute__((__const__))
#define	__unused	__attribute__((__unused__))
#define	__used		__attribute__((__used__))
#define	__packed	__attribute__((__packed__))
#define	__aligned(x)	__attribute__((__aligned__(x)))
#define	__printf0like(fmtarg, firstvararg)	\
		    __attribute__((__format__ (__printf0__, fmtarg, firstvararg)))
#define	__printf01like(fmtarg, firstvararg)	\
		    __attribute__((__format__ (__printf0__, fmtarg, firstvararg)))
#define	__printflike(fmtarg, firstvararg)	\
		    __attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#define	__scanflike(fmtarg, firstvararg)	\
		    __attribute__((__format__ (__scanf__, fmtarg, firstvararg)))
#define	__section(x)	__attribute__((__section__(x)))
#define	__weakref(name)	__attribute__((__weakref__(#name)))
#define	__weak		__attribute__((__weak__))
#define	__alloc_size(x) __attribute__((__alloc_size__(x)))
#define	__alloc_size2(x, y) __attribute__((__alloc_size__(x, y)))
#else
#define	__dead
#define	__pure
#define	__unused
#define	__used
#define	__packed
#define	__aligned(x)
#define	__printf0like(fmtarg, firstvararg)
#define	__printf01like(fmtarg, firstvararg)
#define	__printflike(fmtarg, firstvararg)
#define	__scanflike(fmtarg, firstvararg)
#define	__section(x)
#define	__weakref(name)
#define	__weak
#define	__alloc_size(x)
#define	__alloc_size2(x, y)
#endif

/*
 * Macro for declaring attributes for function arguments.
 */
#if defined(__GNUC__) || defined(__clang__)
#define __arg_type_safety(x) __attribute__((__type_arg_safety__(x)))
#else
#define __arg_type_safety(x)
#endif

/*
 * Compiler attribute to prevent inlining
 */
#if defined(__GNUC__) || defined(__clang__)
#define	__noinline	__attribute__((__noinline__))
#else
#define	__noinline
#endif

#endif /* !_SYS_CDEFS_H_ */
