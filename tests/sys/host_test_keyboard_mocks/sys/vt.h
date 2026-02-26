#ifndef _SYS_VT_H
#define _SYS_VT_H
#include <sys/tty.h>
typedef struct vt_state { struct tty *tty; } vt_state_t;
int vt_get_active(void);
vt_state_t *vt_get_state(int n);
#endif
