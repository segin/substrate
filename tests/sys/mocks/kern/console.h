#ifndef _KERN_CONSOLE_H
#define _KERN_CONSOLE_H

/* Mock console.h */
typedef struct console_backend console_backend_t;
void console_register(console_backend_t *backend);
void kprint(const char *s);

#endif
