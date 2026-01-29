/*
 * turnstile.h - Turnstile Priority Inheritance Support
 */

#ifndef _KERN_TURNSTILE_H
#define _KERN_TURNSTILE_H

#include <sys/proc.h>

void turnstile_init(void);
void turnstile_block(void *lockobj, thread_t *owner);
void turnstile_release(void *lockobj);
int turnstile_get_inherited_priority(thread_t *t);

#endif
