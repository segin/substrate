#ifndef _HOST_FIXUPS_H
#define _HOST_FIXUPS_H

#ifdef HOST_TEST
// Mock privileged instructions to be empty
#undef __asm__
#define __asm__(...) 

#undef __asm__ volatile
#define __asm__ volatile(...)

#undef asm
#define asm(...)
#endif

#endif