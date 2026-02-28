/*
 * sys/kern/file.h - Internal kernel file management APIs
 */

#ifndef _KERN_FILE_H
#define _KERN_FILE_H

#include <sys/file.h>

/*
 * file_alloc - Allocate a file structure from the system pool
 */
struct file *file_alloc(void);

/*
 * file_free - Free a file structure back to the system pool
 */
void file_free(struct file *f);

/*
 * file_close_ptr - Close a file pointer (decrement refcount, call close_fs if 0)
 */
void file_close_ptr(struct file *f);

#endif /* _KERN_FILE_H */
