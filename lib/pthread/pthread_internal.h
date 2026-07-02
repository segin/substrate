#ifndef _PTHREAD_INTERNAL_H
#define _PTHREAD_INTERNAL_H

/*
 * libpthread-private cross-file declarations — NOT part of the public API.
 * These wire the thread-termination path (pthread_exit / cancellation) to the
 * cleanup-handler stack (pthread_cancel.c) and the thread-specific-data
 * destructors (pthread_extra.c).
 */

/* Run the calling thread's pthread_cleanup_push handlers, LIFO. */
void __pthread_run_cleanup_handlers(void);

/* Run the calling thread's pthread_key TSD destructors. */
void __pthread_tsd_run_destructors(void);

/* Node-based cancellation (no signals).  The cancel_pending flag lives on the
 * target thread's registry node so pthread_cancel (pthread_cancel.c) can post
 * it cross-thread; testcancel / the setters read+consume the current thread's.
 * Defined beside the registry in pthread_create.c. */
int  __pthread_post_cancel(pthread_t thread);   /* set target's pending; 0 or ESRCH */
int  __pthread_cancel_requested(void);          /* current thread's pending flag */
void __pthread_cancel_consume(void);            /* clear current thread's pending */

#endif /* _PTHREAD_INTERNAL_H */
