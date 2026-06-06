/*
 * lib/sys/getpagesize.c
 *
 * getpagesize() wrapper
 */

#include <unistd.h>

/*
 * getpagesize() - Return the number of bytes in a memory page
 */
int sys_getpagesize(void) {
    return 4096;
}
