#ifndef _SYS_TTY_H
#define _SYS_TTY_H
struct tty { int dummy; };
void tty_flip_buffer_push(struct tty *tty, char c);
#endif
