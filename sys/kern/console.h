/*
 * sys/kern/console.h Stub
 * Redirects to sys/drivers/console/console.h for backward compatibility.
 */
#ifndef _KERN_CONSOLE_STUB_H
#define _KERN_CONSOLE_STUB_H

/* 
 * We assume include paths are set up such that we can include by path relative 
 * to sys/include or similar, OR we use the full path relative to sys root if -I sys is used.
 * The build uses -I sys.
 * So <drivers/console/console.h> should work.
 */
#include <drivers/console/console.h>

#endif
