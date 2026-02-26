#ifndef _KERN_CONSOLE_H
#define _KERN_CONSOLE_H
void kprint(const char *fmt, ...);
void console_push_char(char c);
#endif
